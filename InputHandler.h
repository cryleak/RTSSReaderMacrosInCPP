#pragma once

#include <windows.h>
#include <tchar.h>
#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <regex>

class InputHandler
{
public:
    static InputHandler& getInstance()
    {
        static InputHandler instance;
        return instance;
    }

    struct Task
    {
        int delay;
        std::optional<std::function<void()>> function;
        bool recursive;
        std::chrono::steady_clock::time_point readyAt{};
    };

    struct Coordinates
    {
        double x;
        double y;

        Coordinates(double x, double y) : x(x), y(y)
        {
        }
    };

    void queueTask(Task task);
    void queueTask(int delay, std::optional<std::function<void()>> function, bool recursive);
    void queueTaskAfter(std::chrono::milliseconds delay, std::function<void()> function);
    void queueInput(WORD vkCode, std::optional<bool> state, bool recursive);
    void queueInputs(std::vector<std::string> inputs, std::function<void()> callback = nullptr);
    void queueMouseMove(double x, double y, bool recursive);

    void executeFirstQueuedTask();
    bool hasQueuedTasks();
    void clearQueuedTasks();

    bool getPhysicalKeyState(WORD vkCode);
    void sendKeyInput(WORD vkCode, bool pressDown);
    void sendAutoHotkeyMouseMove(int x, int y);

    void lockCursorTo(double x, double y);
    void releaseCursor();

    std::optional<WORD> findKey(const std::string& keyToFind);
    int tasksPerformed = 0;
    std::queue<Task> queuedTasks;

private:
    InputHandler();
    ~InputHandler() = default;
    InputHandler(const InputHandler&) = delete;
    void operator=(const InputHandler&) = delete;

    LPCTSTR GetCursorType();
    Coordinates getPixelCoordinates(double x, double y);
    Coordinates getPixelCoordinatesReverse(double pixelX, double pixelY);
    void moveToPixelCoordinates(double x, double y);

    HKL usLayout;
    std::mutex queuedTasksMutex;
    std::regex inputPattern;
};
