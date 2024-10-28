#pragma once

class Lane;
struct lua_State;

// The chain is ended by '(Lane*)(-1)', not nullptr:
// 'trackingFirst -> ... -> ... -> (-1)'
#define TRACKING_END ((Lane*) (-1))

class LaneTracker
{
    private:
    mutable std::mutex trackingMutex;
    Lane* volatile trackingFirst{ nullptr }; // will change to TRACKING_END if we want to activate tracking

    public:
    void tracking_add(Lane* lane_);
    [[nodiscard]] bool tracking_remove(Lane* lane_);
    [[nodiscard]] int pushThreadsTable(lua_State* L_) const;
    void activate() {
        trackingFirst = TRACKING_END;
    }
    [[nodiscard]] bool isActive() const {
        return trackingFirst != nullptr;
    }
};
