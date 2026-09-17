#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <vector>

// Educational client prediction + server reconciliation sample.
// No game process is read, modified, or injected.

constexpr float kFixedDelta = 1.0f / 64.0f;
constexpr float kAcceleration = 34.0f;
constexpr float kFriction = 8.0f;
constexpr float kMaxSpeed = 320.0f;
constexpr float kCorrectionEpsilon = 0.01f;

struct Vec2 {
    float x{};
    float y{};

    Vec2 operator+(Vec2 rhs) const { return {x + rhs.x, y + rhs.y}; }
    Vec2 operator-(Vec2 rhs) const { return {x - rhs.x, y - rhs.y}; }
    Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    Vec2& operator+=(Vec2 rhs) { x += rhs.x; y += rhs.y; return *this; }
};

float Length(Vec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

struct InputCommand {
    std::uint32_t sequence{};
    std::uint32_t clientTick{};
    float forwardMove{};
    float sideMove{};
    bool jumpPressed{};
};

struct PlayerState {
    Vec2 position{};
    Vec2 velocity{};
    std::uint32_t simulationTick{};
    std::uint32_t lastProcessedCommand{};
    bool onGround{true};
};

struct ServerSnapshot {
    PlayerState state{};
    std::uint32_t acknowledgedCommand{};
    std::uint32_t serverTick{};
};

Vec2 ClampSpeed(Vec2 velocity) {
    const float speed = Length(velocity);
    return speed > kMaxSpeed ? velocity * (kMaxSpeed / speed) : velocity;
}

void SimulateMovement(PlayerState& state, const InputCommand& command, float dt) {
    Vec2 wishDirection{command.forwardMove, command.sideMove};
    const float wishLength = Length(wishDirection);
    if (wishLength > 1.0f) wishDirection = wishDirection * (1.0f / wishLength);

    state.velocity += wishDirection * (kAcceleration * dt);
    state.velocity = ClampSpeed(state.velocity);

    if (wishLength < 0.01f && state.onGround) {
        const float damping = std::max(0.0f, 1.0f - kFriction * dt);
        state.velocity = state.velocity * damping;
    }

    if (command.jumpPressed && state.onGround) {
        state.velocity.y -= 240.0f;
        state.onGround = false;
    }

    state.position += state.velocity * dt;
    if (state.position.y >= 0.0f) {
        state.position.y = 0.0f;
        state.velocity.y = 0.0f;
        state.onGround = true;
    } else {
        state.velocity.y += 600.0f * dt;
    }

    state.simulationTick++;
    state.lastProcessedCommand = command.sequence;
}

class AuthoritativeServer {
public:
    void ReceiveInput(const InputCommand& command) {
        inputQueue_.push_back(command);
    }

    ServerSnapshot Tick() {
        while (!inputQueue_.empty() && inputQueue_.front().clientTick <= serverTick_) {
            SimulateMovement(state_, inputQueue_.front(), kFixedDelta);
            inputQueue_.pop_front();
        }
        ++serverTick_;
        return {state_, state_.lastProcessedCommand, serverTick_};
    }

private:
    PlayerState state_{};
    std::deque<InputCommand> inputQueue_;
    std::uint32_t serverTick_{};
};

class PredictedClient {
public:
    InputCommand BuildInput(float forward, float side, bool jump) {
        return {++nextSequence_, localTick_++, forward, side, jump};
    }

    void Predict(const InputCommand& command) {
        SimulateMovement(predicted_, command, kFixedDelta);
        pending_.push_back(command);
    }

    void Reconcile(const ServerSnapshot& snapshot) {
        if (snapshot.acknowledgedCommand <= lastAcknowledged_) return;
        lastAcknowledged_ = snapshot.acknowledgedCommand;

        while (!pending_.empty() && pending_.front().sequence <= snapshot.acknowledgedCommand) {
            pending_.pop_front();
        }

        const float error = Length(predicted_.position - snapshot.state.position);
        if (error < kCorrectionEpsilon) return;

        // The server wins. Then replay only commands it has not acknowledged.
        predicted_ = snapshot.state;
        for (const InputCommand& command : pending_) {
            SimulateMovement(predicted_, command, kFixedDelta);
        }
    }

    const PlayerState& State() const { return predicted_; }
    std::size_t PendingCommandCount() const { return pending_.size(); }

private:
    PlayerState predicted_{};
    std::deque<InputCommand> pending_;
    std::uint32_t nextSequence{};
    std::uint32_t localTick{};
    std::uint32_t lastAcknowledged{};
};

int main() {
    AuthoritativeServer server;
    PredictedClient client;
    std::deque<std::pair<int, InputCommand>> delayedInputs;
    std::deque<std::pair<int, ServerSnapshot>> delayedSnapshots;
    constexpr int latencyTicks = 6;

    for (int tick = 0; tick < 96; ++tick) {
        InputCommand input = client.BuildInput(tick < 64 ? 1.0f : 0.0f, 0.0f, tick == 20);
        client.Predict(input);
        delayedInputs.push_back({tick + latencyTicks, input});

        while (!delayedInputs.empty() && delayedInputs.front().first <= tick) {
            server.ReceiveInput(delayedInputs.front().second);
            delayedInputs.pop_front();
        }

        delayedSnapshots.push_back({tick + latencyTicks, server.Tick()});
        while (!delayedSnapshots.empty() && delayedSnapshots.front().first <= tick) {
            client.Reconcile(delayedSnapshots.front().second);
            delayedSnapshots.pop_front();
        }

        const PlayerState& state = client.State();
        std::cout << "tick=" << std::setw(2) << tick
                  << " predictedX=" << std::fixed << std::setprecision(2) << state.position.x
                  << " pending=" << client.PendingCommandCount() << std::endl;
    }
}
