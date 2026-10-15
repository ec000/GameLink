#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gamelink {
enum class State { Disconnected, Connecting, NatTraversal, Direct, Relay, Unknown, Failed };
enum class Policy { PreferredDirect, DirectRequired, RelayAllowed };
const char* name(State state);
struct Path {
 std::string address;
 bool preferred{}, active{}, expired{};
 std::int64_t lastReceive{};
};
struct Peer {
 std::string nodeId, role;
 bool schemaKnown{}, tunneled{};
 std::vector<Path> paths;
};
struct Sample {
 bool serviceAvailable{}, online{}, joined{}, tcpFallback{};
 std::int64_t clockMs{}, ageMs{}, joiningElapsedMs{};
 std::optional<Peer> peer;
};
struct Decision { State state; std::string reason, physicalPath; };
Decision classify(const Sample& sample);
bool strictDirectSupported();
struct Gate { bool canLaunch; std::string reason; };
Gate launchGate(Policy policy, State state, bool probeHealthy, bool observeOnly);
struct Metrics { double rttMs{}, jitterMs{}, lossPercent{}; std::size_t received{}, sent{}; };
Metrics summarize(const std::vector<std::optional<double>>& rtts);
}
