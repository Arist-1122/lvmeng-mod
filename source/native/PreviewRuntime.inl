#pragma once
#include "PreviewWindow.hpp"
#include <mutex>
#pragma comment(lib,"user32.lib")

std::atomic<uint64_t> primary_preview_until{0};
std::atomic<uint64_t> primary_body_seen{0};
std::atomic<bool> primary_preview_interrupted{false};
std::atomic<HWND> primary_game_window{nullptr};
std::mutex primary_swapchain_state;
api::swapchain* primary_swapchain=nullptr;

void primary_swapchain_created(api::swapchain* swapchain,bool) {
    if (!swapchain || !swapchain->get_device() ||
        swapchain->get_device()->get_api()!=api::device_api::d3d12) return;
    auto* hwnd=GetAncestor(static_cast<HWND>(swapchain->get_hwnd()),GA_ROOT);
    DWORD pid=0;
    if (!hwnd || !GetWindowThreadProcessId(hwnd,&pid) || pid!=GetCurrentProcessId()) return;
    std::lock_guard<std::mutex> lock(primary_swapchain_state);
    if (!primary_swapchain || primary_swapchain==swapchain) {
        primary_swapchain=swapchain;
        primary_game_window.store(hwnd,std::memory_order_release);
    }
}
void primary_swapchain_destroyed(api::swapchain* swapchain,bool) {
    std::lock_guard<std::mutex> lock(primary_swapchain_state);
    if (primary_swapchain!=swapchain) return;
    primary_swapchain=nullptr;
    primary_game_window.store(nullptr,std::memory_order_release);
    primary_preview_until.store(0,std::memory_order_release);
    primary_body_seen.store(0,std::memory_order_release);
    primary_preview_interrupted.store(true,std::memory_order_release);
}
bool primary_focused() {
    const auto hwnd=primary_game_window.load(std::memory_order_acquire);
    return hwnd && GetForegroundWindow()==hwnd;
}
bool primary_recent_body(uint64_t now) {
    const auto seen=primary_body_seen.load(std::memory_order_acquire);
    return seen && seen<=now && now-seen<=7000;
}
bool primary_ready_for_draw() {
    const auto until=primary_preview_until.load(std::memory_order_acquire);
    if (!until) return false;
    const auto now=GetTickCount64();
    if (now>=until || !primary_focused()) {
        primary_preview_until.store(0,std::memory_order_release);
        primary_preview_interrupted.store(true,std::memory_order_release);
        return false;
    }
    return primary_recent_body(now);
}
