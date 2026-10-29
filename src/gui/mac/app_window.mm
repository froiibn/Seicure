#include "gui/mac/app_window.hpp"

#import <AppKit/AppKit.h>

#include <atomic>
#include <memory>
#include <thread>

#include "core/secure_buffer.hpp"
#include "gui/mac/canvas_cg.hpp"
#include "gui/ui/theme.hpp"
#include "job/job.hpp"
#include "platform/file.hpp"

using seicure::SecureBuffer;
using seicure::gui::Hot;
using seicure::gui::LogLine;
using seicure::gui::PendingAction;
using seicure::gui::View;
// Point and Rect cannot be pulled in by name: MacTypes.h has declared both in
// the global namespace since Carbon, and an unqualified mention is ambiguous.
namespace ui = seicure::gui;
namespace theme = seicure::gui::theme;
namespace job = seicure::job;
namespace platform = seicure::platform;

namespace {

std::wstring to_wstring(NSString* s) {
    if (s == nil) {
        return {};
    }
    NSData* data = [s dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
    return std::wstring(static_cast<const wchar_t*>(data.bytes),
                        data.length / sizeof(wchar_t));
}

NSColor* to_nscolor(seicure::gui::Color c) {
    return [NSColor colorWithSRGBRed:c.r / 255.0
                               green:c.g / 255.0
                                blue:c.b / 255.0
                               alpha:1.0];
}

// The traffic lights occupy the left of the caption strip, so the app mark has
// to start clear of them. 62 points past the standard padding puts it just
// after the close/minimise/zoom cluster.
constexpr int kTrafficLightInset = 62;

}  // namespace

// ---------------------------------------------------------------------------

@class SeicureController;

// The one drawn surface. Flipped so y grows downward and the shared layout
// needs no coordinate juggling.
@interface SeicureContentView : NSView
@property(assign, nonatomic) SeicureController* owner;
@end

@interface SeicureController : NSObject <NSWindowDelegate, NSTextFieldDelegate>
@property(strong, nonatomic) NSWindow* window;
@property(strong, nonatomic) SeicureContentView* content;
@property(strong, nonatomic) NSSecureTextField* password;
@property(strong, nonatomic) NSTimer* caretTimer;

- (instancetype)initWithFiles:(std::vector<std::wstring>)files
                       action:(PendingAction)action;
- (View*)view;
- (void)redraw;
- (void)relayout;
- (void)handleClickAt:(ui::Point)pt;
- (void)handleHoverAt:(ui::Point)pt;
- (BOOL)isCaptionDragAt:(ui::Point)pt;
- (void)addFiles:(std::vector<std::wstring>)paths;
- (void)runPendingAction;
- (void)drawInto:(CGContextRef)ctx;
@end

// ---------------------------------------------------------------------------
// The C++ state the controller owns.
// ---------------------------------------------------------------------------

namespace {

struct Impl {
    View                       view;
    seicure::gui::mac::Fonts   fonts;
    CGImageRef                 icon = nullptr;

    PendingAction     pending = PendingAction::None;
    std::thread       worker;
    std::atomic<bool> busy{false};
    std::atomic<bool> cancel{false};
    bool              closing = false;

    ~Impl() {
        cancel.store(true);
        if (worker.joinable()) {
            worker.join();
        }
        fonts.destroy();
        if (icon != nullptr) {
            CGImageRelease(icon);
        }
    }
};

Impl* g_impl = nullptr;

}  // namespace

// ---------------------------------------------------------------------------

@implementation SeicureContentView

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    CGContextRef ctx =
        static_cast<CGContextRef>(NSGraphicsContext.currentContext.CGContext);
    [self.owner drawInto:ctx];
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* area in self.trackingAreas) {
        [self removeTrackingArea:area];
    }
    NSTrackingArea* area = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                     NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
               owner:self
            userInfo:nil];
    [self addTrackingArea:area];
}

- (ui::Point)pointFor:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    return ui::Point{static_cast<int>(p.x), static_cast<int>(p.y)};
}

- (void)mouseMoved:(NSEvent*)event {
    [self.owner handleHoverAt:[self pointFor:event]];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    [self.owner handleHoverAt:ui::Point{-1, -1}];
}

- (void)mouseDown:(NSEvent*)event {
    const ui::Point pt = [self pointFor:event];
    // Dragging the caption is the system's job: performWindowDragWithEvent
    // brings double-click-to-zoom, snapping and Stage Manager with it.
    if ([self.owner isCaptionDragAt:pt]) {
        [self.window performWindowDragWithEvent:event];
        return;
    }
    [self.owner handleClickAt:pt];
}

// --- drag and drop ---------------------------------------------------------

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls = [sender.draggingPasteboard
        readObjectsForClasses:@[ [NSURL class] ]
                      options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    std::vector<std::wstring> paths;
    for (NSURL* url in urls) {
        NSNumber* isDirectory = nil;
        [url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:nil];
        if (isDirectory.boolValue) {
            // Directories are not handled yet; silently skipping would be
            // worse than saying so.
            g_impl->view.append_log(
                job::Event::Error,
                to_wstring(url.lastPathComponent) +
                    L": folders are not supported yet");
            continue;
        }
        paths.push_back(to_wstring(url.path));
    }
    [self.owner addFiles:std::move(paths)];
    return YES;
}

@end

// ---------------------------------------------------------------------------

@implementation SeicureController

- (instancetype)initWithFiles:(std::vector<std::wstring>)files
                       action:(PendingAction)action {
    self = [super init];
    if (self == nil) {
        return nil;
    }

    g_impl = new Impl();
    g_impl->fonts.create();

    // Read the .icns out of the bundle rather than asking NSApp for it.
    // applicationIconImage goes through Launch Services, which has not
    // necessarily noticed a freshly built app and hands back the generic
    // placeholder when it hasn't.
    NSString* iconPath = [NSBundle.mainBundle pathForResource:@"Seicure"
                                                       ofType:@"icns"];
    NSImage* appIcon = iconPath != nil
                           ? [[NSImage alloc] initWithContentsOfFile:iconPath]
                           : NSApp.applicationIconImage;
    if (appIcon != nil) {
        // Ask for the size the caption actually draws, doubled for Retina. The
        // .icns holds a separately drawn keyhole-only variant at the small
        // sizes, and proposing the full 512 would pick the page artwork and
        // shrink it into illegibility.
        NSRect box = NSMakeRect(0, 0, 40, 40);
        CGImageRef cg = [appIcon CGImageForProposedRect:&box
                                                context:nil
                                                  hints:nil];
        if (cg != nullptr) {
            g_impl->icon = CGImageRetain(cg);
        }
    }

    // macOS keeps its own traffic lights in the caption strip, so the shared
    // view draws no window buttons and pushes the mark clear of them instead.
    g_impl->view.set_caption_style(
        seicure::gui::CaptionStyle{false, kTrafficLightInset});

    const NSRect frame =
        NSMakeRect(0, 0, theme::WINDOW_W, theme::WINDOW_H);
    // FullSizeContentView is what makes the app header the title bar. On
    // Windows the same effect costs a WM_NCCALCSIZE dance; here it is a flag.
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable |
                            NSWindowStyleMaskFullSizeContentView
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.titlebarAppearsTransparent = YES;
    self.window.titleVisibility = NSWindowTitleHidden;
    self.window.backgroundColor = to_nscolor(theme::BG);
    self.window.minSize = NSMakeSize(theme::MIN_W, theme::MIN_H);
    self.window.delegate = self;
    self.window.title = @"Seicure";

    self.content = [[SeicureContentView alloc] initWithFrame:frame];
    self.content.owner = self;
    [self.content registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    self.window.contentView = self.content;

    // The one real control on the screen. Everything else is drawn.
    self.password = [[NSSecureTextField alloc] initWithFrame:NSZeroRect];
    self.password.bordered = NO;
    self.password.drawsBackground = NO;
    self.password.focusRingType = NSFocusRingTypeNone;
    self.password.textColor = to_nscolor(theme::TEXT);
    self.password.font = [NSFont monospacedSystemFontOfSize:13
                                                     weight:NSFontWeightRegular];
    self.password.delegate = self;
    self.password.target = self;
    self.password.action = @selector(passwordEntered:);
    [self.content addSubview:self.password];

    [self relayout];
    if (!files.empty()) {
        [self addFiles:std::move(files)];
    }
    [self setPendingAction:action];

    self.caretTimer =
        [NSTimer scheduledTimerWithTimeInterval:0.53
                                        repeats:YES
                                          block:^(NSTimer* timer) {
                                            (void)timer;
                                            View& v = g_impl->view;
                                            v.set_caret_on(!v.caret_on());
                                            [self redraw];
                                          }];

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    return self;
}

- (View*)view {
    return &g_impl->view;
}

- (void)redraw {
    self.content.needsDisplay = YES;
}

- (void)relayout {
    const NSRect b = self.content.bounds;
    View& v = g_impl->view;
    v.set_client(ui::Rect{0, 0, static_cast<int>(b.size.width),
                      static_cast<int>(b.size.height)},
                 96);
    v.compute_layout();

    const ui::Rect field = v.regions().password.inset(12, 9);
    self.password.frame =
        NSMakeRect(field.left, field.top, field.width(), field.height());
}

- (void)drawInto:(CGContextRef)ctx {
    seicure::gui::mac::CgCanvas canvas(ctx, g_impl->fonts, g_impl->icon);
    g_impl->view.paint(canvas);
}

- (BOOL)isCaptionDragAt:(ui::Point)pt {
    // The traffic lights are real subviews and take their own clicks before
    // this is reached, so the whole strip can be treated as draggable.
    return g_impl->view.is_caption_drag(pt) ? YES : NO;
}

- (void)handleHoverAt:(ui::Point)pt {
    View& v = g_impl->view;
    const Hot next = v.hit_test(pt);
    if (next != v.hot()) {
        v.set_hot(next);
        [self redraw];
    }
}

- (void)handleClickAt:(ui::Point)pt {
    View& v = g_impl->view;
    switch (v.hit_test(pt)) {
    case Hot::Panel:
        if (!g_impl->busy.load()) [self browseForFiles];
        break;
    case Hot::ModeIndividual:
        v.set_archive_mode(false);
        [self redraw];
        break;
    case Hot::ModeArchive:
        v.set_archive_mode(true);
        [self redraw];
        break;
    case Hot::Encrypt:
        [self startJob:YES];
        break;
    case Hot::Decrypt:
        [self startJob:NO];
        break;
    case Hot::Clear:
        [self clearFiles];
        break;
    case Hot::Cancel:
        g_impl->cancel.store(true);
        v.set_status(L"cancelling...");
        [self redraw];
        break;
    default:
        [self.window makeFirstResponder:self.content];
        break;
    }
}

- (void)browseForFiles {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = YES;
    panel.canChooseDirectories = NO;
    panel.canChooseFiles = YES;
    if ([panel runModal] != NSModalResponseOK) {
        return;
    }
    std::vector<std::wstring> paths;
    for (NSURL* url in panel.URLs) {
        paths.push_back(to_wstring(url.path));
    }
    [self addFiles:std::move(paths)];
}

- (void)addFiles:(std::vector<std::wstring>)paths {
    View& v = g_impl->view;
    v.add_files(paths);
    const std::size_t count = v.files().size();
    v.set_status(count == 0 ? L"ready"
                            : std::to_wstring(count) + L" file(s) selected");
    [self redraw];
}

- (void)clearFiles {
    if (g_impl->busy.load()) return;
    View& v = g_impl->view;
    v.clear_files();
    v.clear_log();
    v.clear_progress();
    v.set_status(L"ready");
    [self redraw];
}

- (void)setPendingAction:(PendingAction)action {
    g_impl->pending = action;
    if (action == PendingAction::None) {
        return;
    }
    g_impl->view.set_status(
        action == PendingAction::Encrypt
            ? L"enter password, then press Enter to encrypt"
            : L"enter password, then press Enter to decrypt");
    [self.window makeFirstResponder:self.password];
    [self redraw];
}

- (void)passwordEntered:(id)sender {
    (void)sender;
    [self runPendingAction];
}

- (void)runPendingAction {
    if (g_impl->busy.load() || g_impl->view.files().empty()) {
        return;
    }
    [self startJob:(g_impl->pending != PendingAction::Decrypt)];
}

- (SecureBuffer)takePassword {
    NSString* text = self.password.stringValue;
    const char* utf8 = text.UTF8String;
    const std::size_t length = utf8 != nullptr ? std::strlen(utf8) : 0;
    SecureBuffer out(length);
    if (length > 0) {
        std::memcpy(out.data(), utf8, length);
    }
    return out;
}

- (void)startJob:(BOOL)encrypting {
    View& v = g_impl->view;
    if (g_impl->busy.load() || v.files().empty()) {
        return;
    }

    SecureBuffer password = [self takePassword];
    if (password.empty()) {
        v.append_log(job::Event::Error, L"enter a password first");
        [self.window makeFirstResponder:self.password];
        [self redraw];
        return;
    }

    if (g_impl->worker.joinable()) {
        g_impl->worker.join();
    }

    g_impl->busy.store(true);
    g_impl->cancel.store(false);
    v.set_busy(true);
    const std::wstring label = encrypting ? L"encrypting" : L"decrypting";
    v.set_progress(0, label);
    v.set_status(label);
    v.clear_log();
    [self redraw];

    job::Options options;
    options.archive = v.archive_mode() && encrypting;

    auto inputs = v.file_paths();
    auto secret = std::make_shared<SecureBuffer>(std::move(password));
    const bool enc = encrypting == YES;

    g_impl->worker = std::thread([self, inputs, secret, options, enc] {
        auto log = [](job::Event kind, const std::wstring& text) {
            auto* line = new LogLine{kind, text};
            dispatch_async(dispatch_get_main_queue(), ^{
              std::unique_ptr<LogLine> owned(line);
              g_impl->view.append_log(owned->kind, std::move(owned->text));
              g_impl->view.set_status(g_impl->view.busy() ? L"working"
                                                          : L"ready");
            });
        };

        int last_percent = -1;
        auto progress = [&last_percent](int percent, const std::wstring&) {
            if (g_impl->cancel.load()) {
                return false;
            }
            // Only post on change: the engine calls back once per chunk, which
            // would otherwise flood the main queue on a large file.
            if (percent != last_percent) {
                last_percent = percent;
                dispatch_async(dispatch_get_main_queue(), ^{
                  g_impl->view.set_progress_percent(percent);
                });
            }
            return true;
        };

        job::Result result;
        try {
            result = enc ? job::encrypt(inputs, secret->data(), secret->size(),
                                        options, log, progress)
                         : job::decrypt(inputs, secret->data(), secret->size(),
                                        options, log, progress);
        } catch (const std::exception& e) {
            log(job::Event::Error, platform::utf8_to_wide(e.what()));
            result.failed = 1;
        }

        auto* finished = new job::Result(result);
        dispatch_async(dispatch_get_main_queue(), ^{
          std::unique_ptr<job::Result> owned(finished);
          [self finishJob:*owned];
        });
    });

    // The main queue redraws on every callback; a repeating timer would add
    // nothing the progress messages do not already trigger.
    [self redraw];
}

- (void)finishJob:(const job::Result&)result {
    View& v = g_impl->view;
    g_impl->busy.store(false);
    v.set_busy(false);
    v.clear_progress();

    if (result.cancelled) {
        v.set_status(L"cancelled");
        v.append_log(job::Event::Info, L"cancelled -- nothing was written");
    } else if (result.failed > 0) {
        v.set_status(std::to_wstring(result.failed) + L" failed");
    } else {
        v.set_status(L"done  " + std::to_wstring(result.succeeded) + L" ok");
    }

    // The password field is cleared on completion so it does not linger on
    // screen between operations.
    self.password.stringValue = @"";
    [self redraw];

    if (g_impl->closing) {
        [self.window close];
    }
}

// --- window delegate -------------------------------------------------------

- (void)windowDidResize:(NSNotification*)note {
    (void)note;
    [self relayout];
    [self redraw];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    if (g_impl->busy.load()) {
        // Let the worker unwind first: killing it mid-write would leave
        // temporary files behind. finishJob completes the close.
        g_impl->closing = true;
        g_impl->cancel.store(true);
        g_impl->view.set_status(L"cancelling...");
        [self redraw];
        return NO;
    }
    return YES;
}

- (void)windowWillClose:(NSNotification*)note {
    (void)note;
    [self.caretTimer invalidate];
    [NSApp terminate:nil];
}

// The password field reports focus this way; the drawn field outline follows.
- (void)controlTextDidBeginEditing:(NSNotification*)note {
    (void)note;
    g_impl->view.set_password_focused(true);
    [self redraw];
}

- (void)controlTextDidEndEditing:(NSNotification*)note {
    (void)note;
    g_impl->view.set_password_focused(false);
    [self redraw];
}

@end

// ---------------------------------------------------------------------------

namespace seicure::gui::mac {

int run(std::vector<std::wstring> files, PendingAction action,
        const std::string& dump_ui_path) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSApp.activationPolicy = NSApplicationActivationPolicyRegular;

        SeicureController* controller =
            [[SeicureController alloc] initWithFiles:std::move(files)
                                              action:action];

        if (!dump_ui_path.empty()) {
            NSView* view = controller.content;
            NSBitmapImageRep* rep =
                [view bitmapImageRepForCachingDisplayInRect:view.bounds];
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
            NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                            properties:@{}];
            NSString* path = [NSString stringWithUTF8String:dump_ui_path.c_str()];
            return [png writeToFile:path atomically:YES] ? 0 : 1;
        }

        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}

}  // namespace seicure::gui::mac
