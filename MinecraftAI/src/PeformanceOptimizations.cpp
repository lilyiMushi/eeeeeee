#include "MinecraftAI.h"

// PerformanceMonitor Implementation
PerformanceMonitor::PerformanceMonitor() {
    frameTimes.resize(FRAME_HISTORY_SIZE, 0.0);
    lastFrameTime = std::chrono::steady_clock::now();
}

void PerformanceMonitor::FrameStart() {
    auto now = std::chrono::steady_clock::now();
    double frameTime = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
    
    frameTimes[frameIndex % FRAME_HISTORY_SIZE] = frameTime;
    frameIndex++;
    lastFrameTime = now;
}

double PerformanceMonitor::GetAverageFrameTime() const {
    double sum = 0.0;
    size_t count = std::min(frameIndex, FRAME_HISTORY_SIZE);
    for (size_t i = 0; i < count; i++) {
        sum += frameTimes[i];
    }
    return count > 0 ? sum / count : 0.0;
}

double PerformanceMonitor::GetFPS() const {
    double avgFrameTime = GetAverageFrameTime();
    return avgFrameTime > 0 ? 1000.0 / avgFrameTime : 0.0;
}

// ThreadPool Implementation
ThreadPool::ThreadPool(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [this] { return stop || !tasks.empty(); });
                    
                    if (stop && tasks.empty()) return;
                    
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    
    condition.notify_all();
    
    for (std::thread& worker : workers) {
        worker.join();
    }
}

// ImageProcessingCache Implementation
cv::Mat ImageProcessingCache::getOrProcess(const cv::Mat& input, 
                                          std::function<cv::Mat(const cv::Mat&)> processor) {
    size_t hash = computeHash(input);
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto it = processedImages.find(hash);
    if (it != processedImages.end()) {
        auto now = std::chrono::steady_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - cacheTimestamps[hash]).count();
        
        if (age < CACHE_TIMEOUT_MS) {
            return it->second;
        }
    }
    
    // Process and cache
    cv::Mat result = processor(input);
    processedImages[hash] = result.clone();
    cacheTimestamps[hash] = std::chrono::steady_clock::now();
    
    // Clean old entries
    cleanOldEntries();
    
    return result;
}

size_t ImageProcessingCache::computeHash(const cv::Mat& mat) {
    if (mat.empty()) return 0;
    
    // Simple hash based on image properties and sample pixels
    size_t hash = std::hash<int>{}(mat.rows) ^ 
                 (std::hash<int>{}(mat.cols) << 1) ^
                 (std::hash<int>{}(mat.type()) << 2);
    
    // Sample a few pixels for content-based hashing
    if (mat.rows > 10 && mat.cols > 10) {
        cv::Vec3b pixel1 = mat.at<cv::Vec3b>(mat.rows/4, mat.cols/4);
        cv::Vec3b pixel2 = mat.at<cv::Vec3b>(mat.rows/2, mat.cols/2);
        cv::Vec3b pixel3 = mat.at<cv::Vec3b>(3*mat.rows/4, 3*mat.cols/4);
        
        hash ^= (std::hash<int>{}(pixel1[0] + pixel1[1] + pixel1[2]) << 3);
        hash ^= (std::hash<int>{}(pixel2[0] + pixel2[1] + pixel2[2]) << 4);
        hash ^= (std::hash<int>{}(pixel3[0] + pixel3[1] + pixel3[2]) << 5);
    }
    
    return hash;
}

void ImageProcessingCache::cleanOldEntries() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = cacheTimestamps.begin(); it != cacheTimestamps.end();) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second).count();
        
        if (age > CACHE_TIMEOUT_MS) {
            processedImages.erase(it->first);
            it = cacheTimestamps.erase(it);
        } else {
            ++it;
        }
    }
}

// OptimizedMinecraftBot Implementation
OptimizedMinecraftBot::OptimizedMinecraftBot(HumanizationEngine* h, SkyblockStats* s, 
                                            PlayerDetector* pd, ChatHandler* ch)
    : MinecraftBot(h, s, pd, ch) {
    lastCaptureTime = std::chrono::steady_clock::now();
    lastBlockDetection = std::chrono::steady_clock::now();
}

void OptimizedMinecraftBot::CaptureGameState() {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastCapture = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastCaptureTime).count();
    
    // Only capture if enough time has passed
    if (timeSinceLastCapture < CAPTURE_INTERVAL_MS && !lastScreenshot.empty()) {
        return; // Use cached screenshot
    }
    
    // Capture only the necessary region instead of full screen
    cv::Mat newScreenshot = CaptureOptimizedScreen();
    
    if (!newScreenshot.empty()) {
        lastScreenshot = newScreenshot;
        currentState.screenshot = lastScreenshot;
        lastCaptureTime = now;
        
        // Update ROIs based on current state
        UpdateROIs();
        
        // Process only relevant regions
        ProcessRelevantRegions();
    }
}

cv::Mat OptimizedMinecraftBot::CaptureOptimizedScreen() {
    if (!minecraftWindow) return cv::Mat();
    
    RECT windowRect;
    GetWindowRect(minecraftWindow, &windowRect);
    
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    
    // Only capture the game area, skip title bar and borders
    int gameAreaTop = 30; // Skip title bar
    int gameAreaHeight = he