// platform/macos/capturer_sck.mm — macOS screen capture backend.
// ADR-001 §3.1 (Screen Capture, macOS): ScreenCaptureKit. One SCStream per
// SCDisplay; frames are delivered IOSurface-backed (CVPixelBuffer).
//
// Compiled only on (APPLE AND REDESK_USE_REAL_BACKENDS).
//
// This backend implements the *core* ICapturer surface (core/capture/capturer.h:
// push-based setFrameCallback/start/stop, namespace redesk::capture, camelCase).
// An earlier skeleton assumed a pull-based PascalCase shape; that was a
// cross-slice guess (see platform/platform_backends.h note) and is corrected here
// to match the real core interface.
//
// Frame path for the Phase-1 demo: we request BGRA from SCK and copy the pixels
// into VideoFrame::cpu_pixels so the UI can present via a simple QImage path. The
// zero-copy IOSurface->MTLTexture path (VideoFrame::native_handle) is the ADR §3.5
// optimization and is left as a TODO — correctness/visible-result first.
//
// ADR corrections honored / noted:
//   * TCC: Screen Recording must be re-confirmed roughly monthly and after every
//     reboot on macOS 15/26; a denied grant surfaces as ErrorCode::PermissionDenied
//     so the UI can drive re-grant onboarding. A signed, notarized .app is required
//     for the app to appear in the Screen Recording list on macOS 26.1+.
//   * Pre-login: SCK fails at the Login Window from a daemon — post-login only.

#include "platform/platform_backends.h"

#if REDESK_USE_REAL_BACKENDS

#include "core/capture/capturer.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <dispatch/dispatch.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Objective-C stream-output delegate. Bridges SCK's CMSampleBuffer callbacks
// back to the C++ capturer via a function pointer + opaque context.
// ---------------------------------------------------------------------------
@interface RedeskStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, assign) void* owner;            // SckCapturer*
@property(nonatomic, assign) void (*onSample)(void*, CMSampleBufferRef);
@end

@implementation RedeskStreamOutput
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeScreen) return;
    if (self.onSample && self.owner) self.onSample(self.owner, sampleBuffer);
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    (void)stream;
    (void)error;  // TODO(ADR §3.1): surface AccessLost-equivalent to the session.
}
@end

namespace redesk::platform {
namespace {

// Block until an async SCK completion handler fires. SCK's content/start APIs
// are all completion-handler based; for a synchronous C++ start() we gate on a
// small latch. (Capture itself stays async on the SCK dispatch queue.)
//
// `schedule` is an Objective-C block (not a C++ lambda) so the completion
// handlers it installs can capture __block variables in the enclosing scope.
void runAndWait(void (^schedule)(dispatch_semaphore_t)) {
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    schedule(sem);
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW,
                                               (int64_t)(10 * NSEC_PER_SEC)));
}

class SckCapturer final : public redesk::capture::ICapturer {
public:
    SckCapturer() {
        output_ = [[RedeskStreamOutput alloc] init];
        output_.owner = this;
        output_.onSample = &SckCapturer::sampleTrampoline;
        queue_ = dispatch_queue_create("com.redesk.sck", DISPATCH_QUEUE_SERIAL);
    }
    ~SckCapturer() override { stop(); }

    redesk::Result<std::vector<redesk::capture::DisplayInfo>>
    enumerateDisplays() override {
        using R = redesk::Result<std::vector<redesk::capture::DisplayInfo>>;
        __block R result = R::fail(redesk::ErrorCode::Internal, "no content");
        runAndWait(^(dispatch_semaphore_t sem) {
            [SCShareableContent
                getShareableContentExcludingDesktopWindows:NO
                                       onScreenWindowsOnly:NO
                                         completionHandler:^(SCShareableContent* content,
                                                             NSError* error) {
                  if (error || !content) {
                      result = R::fail(redesk::ErrorCode::PermissionDenied,
                                       "SCShareableContent failed (Screen Recording "
                                       "permission?)");
                  } else {
                      std::vector<redesk::capture::DisplayInfo> out;
                      for (SCDisplay* d in content.displays) {
                          redesk::capture::DisplayInfo info;
                          info.id = std::to_string((unsigned)d.displayID);
                          info.name = "Display " + info.id;
                          info.size = {(uint32_t)d.width, (uint32_t)d.height};
                          info.isPrimary =
                              (CGMainDisplayID() == d.displayID);
                          out.push_back(std::move(info));
                      }
                      result = R::good(std::move(out));
                  }
                  dispatch_semaphore_signal(sem);
                }];
        });
        return result;
    }

    void setFrameCallback(redesk::capture::FrameCallback cb) override {
        frame_cb_ = std::move(cb);
    }
    void setCursorCallback(redesk::capture::CursorCallback cb) override {
        cursor_cb_ = std::move(cb);
    }

    redesk::Status start(const redesk::capture::CaptureConfig& cfg) override {
        if (capturing_) return redesk::Status::success();

        // Resolve the requested SCDisplay.
        __block SCDisplay* target = nil;
        __block SCShareableContent* sharedContent = nil;
        __block NSError* contentErr = nil;
        runAndWait(^(dispatch_semaphore_t sem) {
            [SCShareableContent
                getShareableContentExcludingDesktopWindows:NO
                                       onScreenWindowsOnly:NO
                                         completionHandler:^(SCShareableContent* content,
                                                             NSError* error) {
                  sharedContent = content;
                  contentErr = error;
                  dispatch_semaphore_signal(sem);
                }];
        });
        if (contentErr || !sharedContent) {
            return redesk::Status::error(
                redesk::ErrorCode::PermissionDenied,
                "Screen Recording permission denied or unavailable");
        }
        unsigned wantId = cfg.display_id.empty()
                              ? (unsigned)CGMainDisplayID()
                              : (unsigned)std::stoul(cfg.display_id);
        for (SCDisplay* d in sharedContent.displays) {
            if ((unsigned)d.displayID == wantId) { target = d; break; }
        }
        if (!target && sharedContent.displays.count > 0)
            target = sharedContent.displays.firstObject;
        if (!target)
            return redesk::Status::error(redesk::ErrorCode::NotFound,
                                         "no display to capture");

        SCContentFilter* filter =
            [[SCContentFilter alloc] initWithDisplay:target
                                    excludingWindows:@[]];

        SCStreamConfiguration* sc = [[SCStreamConfiguration alloc] init];
        sc.width = (size_t)target.width;
        sc.height = (size_t)target.height;
        sc.minimumFrameInterval =
            CMTimeMake(1, (int32_t)(cfg.target_fps ? cfg.target_fps : 60));
        // BGRA for the simple CPU/QImage demo path (ADR §3.5 zero-copy is a TODO).
        sc.pixelFormat = kCVPixelFormatType_32BGRA;
        sc.showsCursor = cfg.capture_cursor ? YES : NO;
        sc.queueDepth = 5;

        width_ = (uint32_t)target.width;
        height_ = (uint32_t)target.height;

        stream_ = [[SCStream alloc] initWithFilter:filter
                                     configuration:sc
                                          delegate:output_];
        NSError* addErr = nil;
        [stream_ addStreamOutput:output_
                            type:SCStreamOutputTypeScreen
              sampleHandlerQueue:queue_
                           error:&addErr];
        if (addErr)
            return redesk::Status::error(redesk::ErrorCode::Internal,
                                         "addStreamOutput failed");

        __block NSError* startErr = nil;
        runAndWait(^(dispatch_semaphore_t sem) {
            [stream_ startCaptureWithCompletionHandler:^(NSError* error) {
              startErr = error;
              dispatch_semaphore_signal(sem);
            }];
        });
        if (startErr)
            return redesk::Status::error(redesk::ErrorCode::Internal,
                                         "startCapture failed");

        capturing_ = true;
        return redesk::Status::success();
    }

    void stop() override {
        if (!capturing_ && !stream_) return;
        capturing_ = false;
        if (stream_) {
            runAndWait(^(dispatch_semaphore_t sem) {
                [stream_ stopCaptureWithCompletionHandler:^(NSError*) {
                  dispatch_semaphore_signal(sem);
                }];
            });
            stream_ = nil;
        }
    }

    bool isCapturing() const override { return capturing_; }

private:
    static void sampleTrampoline(void* owner, CMSampleBufferRef sb) {
        static_cast<SckCapturer*>(owner)->onSample(sb);
    }

    void onSample(CMSampleBufferRef sb) {
        if (!frame_cb_ || !CMSampleBufferIsValid(sb)) return;
        CVImageBufferRef px = CMSampleBufferGetImageBuffer(sb);
        if (!px) return;

        CVPixelBufferLockBaseAddress(px, kCVPixelBufferLock_ReadOnly);
        const uint32_t w = (uint32_t)CVPixelBufferGetWidth(px);
        const uint32_t h = (uint32_t)CVPixelBufferGetHeight(px);
        const size_t srcStride = CVPixelBufferGetBytesPerRow(px);
        const uint8_t* src =
            static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(px));

        redesk::VideoFrame frame;
        frame.size = {w, h};
        frame.format = redesk::PixelFormat::BGRA8;
        frame.timestamp_us = 0;
        // Tight-pack into cpu_pixels (drop row padding) so the UI can wrap it as a
        // QImage with stride == w*4. TODO(ADR §3.5): replace with zero-copy
        // IOSurface->MTLTexture and set native_handle instead.
        const size_t dstStride = (size_t)w * 4;
        frame.cpu_pixels.resize(dstStride * h);
        if (src) {
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(frame.cpu_pixels.data() + y * dstStride,
                            src + (size_t)y * srcStride, dstStride);
        }
        CVPixelBufferUnlockBaseAddress(px, kCVPixelBufferLock_ReadOnly);

        frame_cb_(frame);
    }

    RedeskStreamOutput* output_ = nil;
    SCStream* stream_ = nil;
    dispatch_queue_t queue_ = nullptr;
    redesk::capture::FrameCallback frame_cb_;
    redesk::capture::CursorCallback cursor_cb_;
    std::atomic<bool> capturing_{false};
    uint32_t width_ = 0, height_ = 0;
};

}  // namespace

std::unique_ptr<redesk::capture::ICapturer> CreateNativeCapturer() {
    return std::make_unique<SckCapturer>();
}

}  // namespace redesk::platform

#endif  // REDESK_USE_REAL_BACKENDS
