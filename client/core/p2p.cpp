#include "p2p.hpp"
#include <cmath>
namespace gamelink {
const char* name(State s) {
 switch(s) {
 case State::Disconnected:return "DISCONNECTED";
 case State::Connecting:return "CONNECTING";
 case State::NatTraversal:return "NAT_TRAVERSAL";
 case State::Direct:return "DIRECT";
 case State::Relay:return "RELAY";
 case State::Unknown:return "UNKNOWN";
 case State::Failed:return "FAILED";
 }
 return "UNKNOWN";
}
Decision classify(const Sample& s) {
 if(!s.serviceAvailable || !s.online) return {State::Disconnected,"ZeroTier unavailable/offline",{}};
 if(s.ageMs<0 || s.ageMs>6000) return {State::Unknown,"snapshot expired",{}};
 if(!s.joined) return {State::Connecting,"network join/authorization pending",{}};
 if(!s.peer) return {s.joiningElapsedMs<30000 ? State::NatTraversal : State::Failed,
     "target Node ID absent; NAT cause cannot be determined",{}};
 const auto& p=*s.peer;
 if(!p.schemaKnown || p.role!="LEAF") return {State::Unknown,"unsupported peer schema/role",{}};
 if(p.tunneled || s.tcpFallback) return {State::Relay,"ZeroTier reports TCP fallback active (service scope)",{}};
 bool preferred=false;
 for(const auto& path:p.paths) {
  if(!path.preferred) continue;
  preferred=true;
  if(path.active && !path.expired && !path.address.empty() && path.lastReceive>0 &&
      s.clockMs>=path.lastReceive && s.clockMs-path.lastReceive<=10000)
   return {State::Direct,"fresh preferred active physical path",path.address};
 }
 if(!preferred) return {State::Relay,"no preferred physical path: official CLI fallback inference",{}};
 return {State::Unknown,"preferred path stale/expired/inconsistent",{}};
}
bool strictDirectSupported(){ return false; }
Gate launchGate(Policy p,State s,bool healthy,bool observe) {
 if(p==Policy::DirectRequired) return {false,"Direct Required unsupported: no reliable packet enforcement"};
 if(observe) return {true,"Observe only; Steam path is independent of ZeroTier"};
 if(s==State::Disconnected || s==State::Connecting || s==State::NatTraversal || s==State::Failed)
  return {false,"network is not ready"};
 if(!healthy) return {false,"run a successful virtual IP probe first"};
 return {true,s==State::Direct ? "direct evidence observed" : "fallback/unknown path; relay may occur"};
}
Metrics summarize(const std::vector<std::optional<double>>& values) {
 Metrics m; m.sent=values.size();std::optional<double> previous;double delta=0;std::size_t deltas=0;
 for(const auto& value:values) {
  if(!value || !std::isfinite(*value) || *value<0) {previous.reset();continue;}
  ++m.received;m.rttMs+=*value;
  if(previous){delta+=std::abs(*value-*previous);++deltas;}previous=value;
 }
 if(m.received)m.rttMs/=static_cast<double>(m.received);
 if(deltas)m.jitterMs=delta/static_cast<double>(deltas);
 if(m.sent)m.lossPercent=100.0*static_cast<double>(m.sent-m.received)/static_cast<double>(m.sent);
 return m;
}
}
