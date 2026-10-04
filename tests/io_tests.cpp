#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "io/VirtualIO.hpp"
#include "io/Sensors.hpp"
#include "io/VirtualSensors.hpp"
#include "console/IOCommands.hpp"
#include "protocol/StepperNinjaProtocol.hpp"
#include "recorder/MotionRecorder.hpp"
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x); } while(false)

template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } CHECK(rejected); }
int main() {
    try {
        cnc::VirtualIO io;
        for (std::size_t i=0;i<128;++i) {
            io.set_input(i,true);
            CHECK(io.input(i));
            cnc::InputWords expected{}; expected[i/32]=std::uint32_t{1}<<(i%32);
            CHECK(io.packed_inputs()==expected);
            io.set_input(i,false); CHECK(io.packed_inputs()==cnc::InputWords{});
        }
        for (auto n : {0u,31u,32u,63u,64u,95u,96u,127u}) io.set_input(n,true);
        CHECK(io.packed_inputs()==(cnc::InputWords{0x80000001u,0x80000001u,0x80000001u,0x80000001u}));
        rejects([&]{io.set_input(128,true);}); rejects([&]{io.input(128);});
        io.set_automatic({0,1,0,0}); io.set_input(32,false); CHECK(io.input(32));
        io.clear_manual(); CHECK(io.packed_inputs()==(cnc::InputWords{0,1,0,0}));
        io.set_automatic({}); CHECK(io.packed_inputs()==cnc::InputWords{});

        cnc::AxisSwitch sensor;
        sensor.configure({0,22,true,-100,10});
        for (auto [pos,active] : {std::pair{-99,false},{-100,true},{-99,true},{-91,true},{-90,false},{-99,false}}) {
            sensor.update({pos,0,0,0}); CHECK(sensor.status().active==active);
        }
        sensor.configure({1,26,false,100,10});
        for (auto [pos,active] : {std::pair{99,false},{100,true},{99,true},{91,true},{90,false},{99,false}}) {
            sensor.update({0,pos,0,0}); CHECK(sensor.status().active==active);
        }
        // Phase 3 Z approaches its max/home switch from below in the positive direction.
        sensor.configure({2,27,false,400,40});
        for (auto [pos,active] : {std::pair{399,false},{400,true},{399,true},{361,true},{360,false},{399,false}}) {
            sensor.update({0,0,pos,0}); CHECK(sensor.status().active==active);
        }
        for (bool minimum : {false,true}) {
            sensor.configure({0,22,minimum,0,0});
            for (int i=0;i<100;++i) { sensor.update({}); CHECK(sensor.status().active); }
        }
        sensor.disable(); CHECK(!sensor.status().enabled && !sensor.status().active);
        constexpr auto hi=std::numeric_limits<std::int64_t>::max(), lo=std::numeric_limits<std::int64_t>::min();
        rejects([&]{sensor.configure({0,22,true,hi,1});});
        rejects([&]{sensor.configure({0,22,false,lo,1});});
        rejects([&]{sensor.configure({0,22,true,0,-1});});
        rejects([&]{sensor.configure({3,22,true,0,0});});
        rejects([&]{sensor.configure({0,128,true,0,0});});
        sensor.configure({0,22,true,hi-1,1}); sensor.update({hi-1,0,0,0}); CHECK(sensor.status().active);
        sensor.update({hi,0,0,0}); CHECK(!sensor.status().active);

        cnc::PlaneGeometry plane(2,-20);
        CHECK(!plane.evaluate({0,0,0,0},{0,0,-19,0}).touching);
        const auto contact=plane.evaluate({0,10,-10,0},{20,30,-30,0});
        CHECK(contact.touching && contact.entered && contact.fraction==0.5L);
        CHECK(contact.point==(std::array<long double,3>{10,20,-20}));
        CHECK(plane.evaluate({0,0,-19,0},{0,0,-20,0}).touching);
        CHECK(!plane.evaluate({0,0,-21,0},{0,0,-19,0}).touching);
        const auto extreme=plane.evaluate({0,0,hi,0},{0,0,lo,0});
        CHECK(extreme.touching && extreme.entered && extreme.fraction>=0 && extreme.fraction<=1);
        rejects([]{cnc::PlaneGeometry invalid(3,0);});

        cnc::VirtualSensors sensors;
        auto apply=[&](const std::string& text,cnc::StepPositions p=cnc::StepPositions{}) {sensors.execute(cnc::parse_io_command(text),p);};
        apply("limits set X min -10 2 22"); apply("limits set X max 10 2 22");
        apply("limits set Y min -10 2 26"); apply("probe plane Z -20 28");
        sensors.update({}, {-10,-10,-20,0});
        CHECK(sensors.inputs()[0]==((1u<<22)|(1u<<26)|(1u<<28)));
        sensors.update({-10,-10,-20,0}, {10,0,0,0});
        CHECK(sensors.inputs()[0]==(1u<<22)); // OR of both switches sharing a pin.
        apply("input 22 on",{10,0,0,0}); sensors.update({10,0,0,0},{}); CHECK(sensors.inputs()[0]==(1u<<22));
        apply("input 22 off"); CHECK(sensors.inputs()==cnc::InputWords{});
        apply("probe off"); sensors.update({}, {0,0,-100,0}); CHECK(sensors.inputs()==cnc::InputWords{});
        for (const auto& bad : {"input -1 on","input 128 off","input 3 true","input 0 on extra","input show extra", "input 1.0 on", "limits set A min 0 1 22", "limits set X min 1 -1 22", "limits set X min 9223372036854775807 1 22", "probe plane Z 0 128", "probe plane Z nan 28", "limits off X wrong"}) rejects([&]{cnc::parse_io_command(bad);});

        // Exercise the production packet-boundary ordering, including malformed input.
        cnc::StepperNinjaProtocol protocol;
        cnc::MotionRecorder recorder;
        cnc::VirtualSensors machine_io;
        machine_io.execute(cnc::parse_io_command("limits set X min -10 2 22"),{});
        machine_io.execute(cnc::parse_io_command("probe plane Z -20 28"),{});
        recorder.begin({},0);
        cnc::Request request{}; request.pio_timing=235;
        auto cycle=[&](std::int64_t x,std::int64_t z,std::uint64_t now,bool valid=true) {
            for(std::size_t i=0;i<4;++i)request.stepgen_command[i]=0;
            for(auto [axis,delta]:{std::pair{0,x},{2,z}}) if(delta) request.stepgen_command[axis]=(delta>0?0x80000000u:0u)|(1234u<<10)|static_cast<unsigned>(std::abs(delta)-1);
            request.checksum=calculate_checksum(&request,sizeof(request)-1); if(!valid)request.checksum^=1;
            auto previous=protocol.machine().positions();
            auto accepted=protocol.accept({reinterpret_cast<const std::uint8_t*>(&request),sizeof(request)},now);
            if(!valid) { CHECK(!accepted); return cnc::Response{}; }
            CHECK(accepted); ++request.packet_id;
            machine_io.update(previous,protocol.machine().positions());
            auto response=protocol.make_response(*accepted,machine_io.inputs());
            CHECK(tx_checksum_ok(&response));
            recorder.observe(protocol.machine().positions(),now);
            return response;
        };
        auto response=cycle(-10,-21,1000);
        CHECK(response.inputs[0]==((1u<<22)|(1u<<28)));
        CHECK(protocol.machine().positions()==(cnc::StepPositions{-10,0,-21,0}));
        CHECK(recorder.count()==2 && recorder.snapshot().tail.back().changed_axes==5);
        cycle(100,100,2000,false); CHECK(protocol.machine().positions()==(cnc::StepPositions{-10,0,-21,0}));
        CHECK(recorder.count()==2 && machine_io.status().probe.active);
        machine_io.execute(cnc::parse_io_command("input 127 on"),protocol.machine().positions());
        for(unsigned i=0;i<100;++i) response=cycle(0,0,3000+i);
        CHECK(recorder.count()==2 && response.inputs[3]==0x80000000u);
        response=cycle(2,2,4000); CHECK(response.inputs[0]==0 && response.inputs[3]==0x80000000u);
        CHECK(recorder.count()==3 && protocol.stats().packet_id_gaps==0);
        std::cout<<"PASS: 128 inputs, OR, range, limit boundaries/hysteresis/overflow including Phase-3 Z max, probe sweep/retract, parser, same-packet response+checksum, recorder\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
