/*
 * SDL2 test with window state verification (SDL2 compatible).
 */
#include <SDL.h>
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif
#include <unistd.h>

int main(int argc, char* argv[]) {
#ifdef __APPLE__
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [[NSProcessInfo processInfo] setProcessName:@"SDL2 Test V2"];
        [NSApp activateIgnoringOtherApps:YES];
    }
    fprintf(stderr, "[Test] NSApplication initialized\n");
#endif

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[Test] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    fprintf(stderr, "[Test] SDL_Init OK\n");

    SDL_Window* win = SDL_CreateWindow("SDL2 Test Window",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "[Test] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    fprintf(stderr, "[Test] Window created, id=%u\n", SDL_GetWindowID(win));

    Uint32 flags = SDL_GetWindowFlags(win);
    fprintf(stderr, "[Test] Window flags: 0x%x\n", flags);
    fprintf(stderr, "[Test]   SHOWN:       %s\n", (flags & SDL_WINDOW_SHOWN) ? "YES" : "NO");
    fprintf(stderr, "[Test]   FOCUS_INPUT: %s\n", (flags & SDL_WINDOW_INPUT_FOCUS) ? "YES" : "NO");
    fprintf(stderr, "[Test]   FOCUS_MOUSE: %s\n", (flags & SDL_WINDOW_MOUSE_FOCUS) ? "YES" : "NO");
    fprintf(stderr, "[Test]   MINIMIZED:   %s\n", (flags & SDL_WINDOW_MINIMIZED) ? "YES" : "NO");
    fprintf(stderr, "[Test]   FULLSCREEN:  %s\n", (flags & SDL_WINDOW_FULLSCREEN) ? "YES" : "NO");

    SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "[Test] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // Draw green
    SDL_SetRenderDrawColor(renderer, 50, 200, 50, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    fprintf(stderr, "[Test] Rendered green screen\n");

    SDL_RaiseWindow(win);

    flags = SDL_GetWindowFlags(win);
    fprintf(stderr, "[Test] After raise - SHOWN: %s, FOCUS_INPUT: %s\n",
        (flags & SDL_WINDOW_SHOWN) ? "YES" : "NO",
        (flags & SDL_WINDOW_INPUT_FOCUS) ? "YES" : "NO");

    // Check NSApp windows
#ifdef __APPLE__
    @autoreleasepool {
        NSArray<NSWindow*>* windows = [NSApp windows];
        fprintf(stderr, "[Test] NSApp has %lu window(s)\n", (unsigned long)[windows count]);
        for (NSWindow* nsWin in windows) {
            NSString* title = [nsWin title];
            NSRect frame = [nsWin frame];
            fprintf(stderr, "[Test]   NSWindow: title='%s', visible=%d, frame=(%.0f,%.0f,%.0fx%.0f)\n",
                [title UTF8String],
                [nsWin isVisible] ? 1 : 0,
                frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
        }
        if ([windows count] == 0) {
            fprintf(stderr, "[Test] WARNING: NSApp has NO windows! SDL may not have created an NSWindow.\n");
        }
    }
#endif

    // Event loop
    fprintf(stderr, "[Test] Running event loop for 10s...\n");
    Uint32 start = SDL_GetTicks();
    SDL_Event event;
    int running = 1;
    int frame_count = 0;
    while (running && (SDL_GetTicks() - start) < 10000) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
                break;
            }
        }
        frame_count++;
        SDL_SetRenderDrawColor(renderer, 50, 200, 50, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(33);
    }
    fprintf(stderr, "[Test] Rendered %d frames. Exiting.\n", frame_count);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
