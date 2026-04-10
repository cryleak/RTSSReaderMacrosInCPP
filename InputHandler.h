#pragma once

#include <windows.h>
#include <tchar.h>
#include <optional>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <regex>

#define USE_MOUSE_WHEEL 1

class InputHandler {
public:
    // Singleton access
    static InputHandler& getInstance() {
        static InputHandler instance;
        return instance;
    }

    struct Task {
        int delay;
        std::optional<std::function<void()>> function;
        bool recursive;
    };

    struct Coordinates {
        double x;
        double y;
        Coordinates(double x, double y) : x(x), y(y) {}
    };

    // Public API
    void queueTask(Task task);
    void queueTask(int delay, std::optional<std::function<void()>> function, bool recursive);
    void queueInput(WORD vkCode, std::optional<bool> state, bool recursive);
    void queueInputs(std::vector<std::string> inputs, std::function<void()> callback = nullptr);
    void queueMouseMove(double x, double y, bool recursive);

    void executeFirstQueuedTask();

    bool getPhysicalKeyState(WORD vkCode);
    void sendKeyInput(WORD vkCode, bool pressDown);

    void lockCursorTo(double x, double y);
    void releaseCursor();

    std::optional<WORD> findKey(const std::string& keyToFind);
    int tasksPerformed = 0;
    std::queue<Task> queuedTasks;

private:
    // Private Constructor/Destructor for Singleton
    InputHandler();
    ~InputHandler() = default;
    InputHandler(const InputHandler&) = delete;
    void operator=(const InputHandler&) = delete;

    // Internal Helpers
    LPCTSTR GetCursorType();
    Coordinates getPixelCoordinates(double x, double y);
    Coordinates getPixelCoordinatesReverse(double pixelX, double pixelY);
    void moveToPixelCoordinates(double x, double y);

    // Members
    HKL usLayout;
    std::mutex queuedTasksMutex;
    std::regex inputPattern;
};