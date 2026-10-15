#include "p2p.hpp"
#include <cstdlib>
#include <iostream>
using namespace gamelink;
void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
int main(){
 Sample s;check(classify(s).state==State::Disconnected,"unavailable");
 s.serviceAvailable=true;s.online=true;s.joined=true;s.clockMs=100000;
 check(classify(s).state==State::NatTraversal,"discovery");
 s.joiningElapsedMs=30000;check(classify(s).state==State::Failed,"bounded traversal");
 s.peer=Peer{"aabbccddee","LEAF",true,false,{{"203.0.113.1/9993",true,true,false,99900}}};
 check(classify(s).state==State::Direct,"direct target path");
 s.peer->tunneled=true;check(classify(s).state==State::Relay,"tcp fallback");s.peer->tunneled=false;
 s.tcpFallback=true;check(classify(s).state==State::Relay,"global fallback");s.tcpFallback=false;
 s.peer->paths.clear();check(classify(s).state==State::Relay,"no preferred CLI inference");
 s.peer->schemaKnown=false;check(classify(s).state==State::Unknown,"schema capability");s.peer->schemaKnown=true;
 s.peer->role="PLANET";check(classify(s).state==State::Unknown,"root is not player");s.peer->role="LEAF";
 s.peer->paths={{"203.0.113.1/9993",true,true,false,80000}};
 check(classify(s).state==State::Unknown,"idle physical path");
 s.peer->paths[0].lastReceive=100001;check(classify(s).state==State::Unknown,"clock inconsistent");
 s.peer->paths[0].lastReceive=99900;s.peer->paths[0].expired=true;
 check(classify(s).state==State::Unknown,"expired path");s.peer->paths[0].expired=false;
 s.ageMs=6001;check(classify(s).state==State::Unknown,"snapshot expiration");s.ageMs=0;
 s.online=false;check(classify(s).state==State::Disconnected,"service restart invalidates");s.online=true;
 s.joined=false;check(classify(s).state==State::Connecting,"network switch invalidates");
 check(!strictDirectSupported(),"strict capability honestly unavailable");
 for(auto state:{State::Direct,State::Relay,State::Unknown,State::Failed})
  check(!launchGate(Policy::DirectRequired,state,true,false).canLaunch,"strict always rejected");
 check(launchGate(Policy::PreferredDirect,State::Relay,true,false).canLaunch,"preferred permits normal fallback");
 check(!launchGate(Policy::PreferredDirect,State::Direct,false,false).canLaunch,"probe required");
 check(launchGate(Policy::PreferredDirect,State::Disconnected,false,true).canLaunch,"observation independent");
 auto m=summarize({10.0,14.0,std::nullopt,20.0,24.0});
 check(m.sent==5 && m.received==4 && m.rttMs==17 && m.jitterMs==4 && m.lossPercent==20,"RTT jitter loss");
 check(summarize({}).lossPercent==0,"empty probe");
 std::cout<<"core state, policy, statistics checks passed\n";
}
