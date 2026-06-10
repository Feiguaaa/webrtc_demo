/*
 * Quick test: match exactly what SdlVideoRenderer constructor does.
 */
#include <SDL.h>
#include <cstdint>
#include <cstring>
#include <vector>
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

int main(int argc, char* argv[]) {
#ifdef __APPLE__
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp activateIgnoringOtherApps:YES];
        [[NSProcessInfo processInfo] setProcessName:@"WebRTC Receiver"];
    }
    fprintf(stderr, "[Test] NSApplication initialized\n");
#endif

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[Test] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window_ = SDL_CreateWindow("WebRTC Receiver",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        fprintf(stderr, "[Test] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    fprintf(stderr, "[Test] Window created, id=%u\n", SDL_GetWindowID(window_));

    SDL_Renderer* sdl_renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl_renderer_) {
        fprintf(stderr, "[Test] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture_ = SDL_CreateTexture(sdl_renderer_, SDL_PIXELFORMAT_BGRA32,
        SDL_TEXTUREACCESS_STREAMING, 640, 480);
    if (!texture_) {
        fprintf(stderr, "[Test] SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(sdl_renderer_);
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 1;
    }

    // Draw a gray screen (exactly like SdlVideoRenderer)
    std::vector<uint8_t> buf(640 * 480 * 4);
    memset(buf.data(), 128, buf.size());
    SDL_UpdateTexture(texture_, nullptr, buf.data(), 640 * 4);
    SDL_RenderClear(sdl_renderer_);
    SDL_RenderCopy(sdl_renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(sdl_renderer_);

    SDL_PumpEvents();
    SDL_RaiseWindow(window_);
    SDL_SetWindowInputFocus(window_);
    SDL_Delay(100);
    SDL_PumpEvents();

    int wx = 0, wy = 0;
    SDL_GetWindowPosition(window_, &wx, &wy);
    fprintf(stderr, "[Test] Window at (%d,%d) %dx%d\n", wx, wy, 640, 480);

    // Now run the same kind of loop as the real app
    fprintf(stderr, "[Test] Running main loop...\n");
    for (int i = 0; i < 300; i++) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                fprintf(stderr, "[Test] Quit event received\n");
                goto done;
            }
        }
        SDL_RenderClear(sdl_renderer_);
        SDL_RenderCopy(sdl_renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer_);
        SDL_Delay(33);
    }
done:
    fprintf(stderr, "[Test] Exiting\n");
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(sdl_renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}
