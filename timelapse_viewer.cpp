#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

class TimelapseViewer {
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::vector<std::string> imagePaths;
    std::vector<SDL_Texture*> textures;
    size_t currentIndex = 0;
    bool running = true;
    bool playing = false;
    int targetFPS = 240;
    bool fullscreen = false;
    int windowWidth = 1280;
    int windowHeight = 720;

public:
    TimelapseViewer() = default;
    
    ~TimelapseViewer() {
        cleanup();
    }
    
    bool initialize(const std::string& directoryPath, bool fullscreenMode, int fps) {
        fullscreen = fullscreenMode;
        targetFPS = fps;
        
        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }
        
        // Initialize SDL_image
        int imgFlags = IMG_INIT_JPG | IMG_INIT_PNG;
        if (!(IMG_Init(imgFlags) & imgFlags)) {
            std::cerr << "SDL_image could not initialize! SDL_image Error: " << IMG_GetError() << std::endl;
            return false;
        }
        
        // Create window
        Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
        if (fullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
        
        window = SDL_CreateWindow("High-Speed Timelapse Viewer", 
                                 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
                                 windowWidth, windowHeight, windowFlags);
        if (!window) {
            std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }
        
        // Create renderer
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }
        
        // Get actual window size (in case of fullscreen)
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        
        // Load images
        if (!loadImagesFromDirectory(directoryPath)) {
            return false;
        }
        
        std::cout << "Initialized successfully with " << imagePaths.size() << " images" << std::endl;
        std::cout << "Target framerate: " << targetFPS << " FPS" << std::endl;
        std::cout << "Controls: Space=Play/Pause, Left/Right=Prev/Next, ESC=Quit" << std::endl;
        
        return true;
    }
    
    bool loadImagesFromDirectory(const std::string& directoryPath) {
        if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
            std::cerr << "Invalid directory path: " << directoryPath << std::endl;
            return false;
        }
        
        // Find all image files
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                
                if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || 
                    extension == ".bmp" || extension == ".tif" || extension == ".tiff") {
                    imagePaths.push_back(entry.path().string());
                }
            }
        }
        
        if (imagePaths.empty()) {
            std::cerr << "No images found in directory: " << directoryPath << std::endl;
            return false;
        }
        
        // Sort paths alphanumerically
        std::sort(imagePaths.begin(), imagePaths.end());
        
        // Pre-load all images into textures for maximum performance
        std::cout << "Loading " << imagePaths.size() << " images..." << std::endl;
        textures.resize(imagePaths.size(), nullptr);
        
        for (size_t i = 0; i < imagePaths.size(); ++i) {
            SDL_Surface* surface = IMG_Load(imagePaths[i].c_str());
            if (!surface) {
                std::cerr << "Unable to load image " << imagePaths[i] << ": " << IMG_GetError() << std::endl;
                continue;
            }
            
            textures[i] = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
            
            if (!textures[i]) {
                std::cerr << "Unable to create texture from " << imagePaths[i] << ": " << SDL_GetError() << std::endl;
            }
            
            // Show loading progress
            if (i % 10 == 0 || i == imagePaths.size() - 1) {
                std::cout << "Loaded " << (i + 1) << "/" << imagePaths.size() << " images\r" << std::flush;
            }
        }
        std::cout << std::endl << "All images loaded successfully!" << std::endl;
        
        return true;
    }
    
    void run() {
        if (imagePaths.empty() || !window || !renderer) {
            std::cerr << "Cannot run: viewer not properly initialized" << std::endl;
            return;
        }
        
        SDL_Event e;
        auto lastFrameTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        auto fpsTimer = std::chrono::high_resolution_clock::now();
        
        while (running) {
            // Handle events
            while (SDL_PollEvent(&e) != 0) {
                if (e.type == SDL_QUIT) {
                    running = false;
                } else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;
                        case SDLK_SPACE:
                            playing = !playing;
                            break;
                        case SDLK_RIGHT:
                            if (!playing) {
                                currentIndex = (currentIndex + 1) % imagePaths.size();
                                renderCurrentFrame();
                            }
                            break;
                        case SDLK_LEFT:
                            if (!playing) {
                                currentIndex = (currentIndex + imagePaths.size() - 1) % imagePaths.size();
                                renderCurrentFrame();
                            }
                            break;
                    }
                }
            }
            
            // Update frame if playing
            if (playing) {
                auto currentTime = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastFrameTime).count();
                
                // Calculate the time per frame in milliseconds
                int msPerFrame = 1000 / targetFPS;
                
                if (elapsed >= msPerFrame) {
                    lastFrameTime = currentTime;
                    currentIndex = (currentIndex + 1) % imagePaths.size();
                    renderCurrentFrame();
                    frameCount++;
                }
                
                // Calculate FPS every second
                auto fpsDuration = std::chrono::duration_cast<std::chrono::seconds>(
                    currentTime - fpsTimer).count();
                if (fpsDuration >= 1) {
                    std::string title = "High-Speed Timelapse Viewer - " + 
                                       std::to_string(frameCount / fpsDuration) + " FPS";
                    SDL_SetWindowTitle(window, title.c_str());
                    frameCount = 0;
                    fpsTimer = currentTime;
                }
            } else {
                // If not playing, just render the current frame and wait
                SDL_Delay(10);
            }
        }
    }
    
    void renderCurrentFrame() {
        if (currentIndex >= textures.size() || !textures[currentIndex]) {
            return;
        }
        
        // Clear screen
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        
        // Get texture dimensions
        int textureWidth, textureHeight;
        SDL_QueryTexture(textures[currentIndex], NULL, NULL, &textureWidth, &textureHeight);
        
        // Calculate scaling to maintain aspect ratio
        float scaleX = static_cast<float>(windowWidth) / textureWidth;
        float scaleY = static_cast<float>(windowHeight) / textureHeight;
        float scale = std::min(scaleX, scaleY);
        
        int renderWidth = static_cast<int>(textureWidth * scale);
        int renderHeight = static_cast<int>(textureHeight * scale);
        
        // Center the image
        int renderX = (windowWidth - renderWidth) / 2;
        int renderY = (windowHeight - renderHeight) / 2;
        
        // Render the texture
        SDL_Rect renderRect = {renderX, renderY, renderWidth, renderHeight};
        SDL_RenderCopy(renderer, textures[currentIndex], NULL, &renderRect);
        
        // Present the renderer
        SDL_RenderPresent(renderer);
    }
    
    void cleanup() {
        // Free textures
        for (auto& texture : textures) {
            if (texture) {
                SDL_DestroyTexture(texture);
                texture = nullptr;
            }
        }
        
        // Destroy renderer and window
        if (renderer) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        
        // Quit SDL subsystems
        IMG_Quit();
        SDL_Quit();
    }
};

int main(int argc, char* argv[]) {
    std::string directoryPath;
    bool fullscreen = false;
    int fps = 240;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-d" || arg == "--directory") {
            if (i + 1 < argc) {
                directoryPath = argv[++i];
            }
        } else if (arg == "-f" || arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--fps") {
            if (i + 1 < argc) {
                fps = std::stoi(argv[++i]);
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -d, --directory PATH   Directory containing image files" << std::endl;
            std::cout << "  -f, --fullscreen       Run in fullscreen mode" << std::endl;
            std::cout << "  --fps N                Target framerate (default: 240)" << std::endl;
            std::cout << "  -h, --help             Show this help message" << std::endl;
            return 0;
        } else if (directoryPath.empty()) {
            // If no explicit directory flag, use the first argument as the directory
            directoryPath = arg;
        }
    }
    
    // Prompt for directory if not provided
    if (directoryPath.empty()) {
        std::cout << "Enter path to directory containing images: ";
        std::getline(std::cin, directoryPath);
    }
    
    TimelapseViewer viewer;
    if (!viewer.initialize(directoryPath, fullscreen, fps)) {
        std::cerr << "Failed to initialize viewer. Exiting." << std::endl;
        return 1;
    }
    
    viewer.run();
    return 0;
}
