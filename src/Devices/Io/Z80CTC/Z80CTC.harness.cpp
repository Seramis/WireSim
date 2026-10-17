#include "Z80CTC.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

Sim::Testing::DeviceHandle CreateZ80CTC(Sim::Platform& p, const Sim::Testing::ArgsMap&) {
	auto dev = std::make_unique<Z80CTC>(&p);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("CS0", Sim::Testing::MakeStub(p, dev->Pins.CS0, "CS0"));
	h.Points.emplace_back("CS1", Sim::Testing::MakeStub(p, dev->Pins.CS1, "CS1"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	h.Points.emplace_back("CLK", Sim::Testing::MakeStub(p, dev->Pins.CLK, "CLK"));
	h.Points.emplace_back("NotM1", Sim::Testing::MakeStub(p, dev->Pins.NotM1, "NotM1"));
	h.Points.emplace_back("NotIORQ", Sim::Testing::MakeStub(p, dev->Pins.NotIORQ, "NotIORQ"));
	h.Points.emplace_back("NotRD", Sim::Testing::MakeStub(p, dev->Pins.NotRD, "NotRD"));
	h.Points.emplace_back("NotRESET", Sim::Testing::MakeStub(p, dev->Pins.NotRESET, "NotRESET"));
	h.Points.emplace_back("IEI", Sim::Testing::MakeStub(p, dev->Pins.IEI, "IEI"));
	h.Points.emplace_back("IEO", Sim::Testing::MakeStub(p, dev->Pins.IEO, "IEO"));
	h.Points.emplace_back("NotINT", Sim::Testing::MakeStub(p, dev->Pins.NotINT, "NotINT"));
	for (int i = 0; i < 4; ++i) {
		const auto name = std::format("ClkTrg{}", i);
		h.Points.emplace_back(name, Sim::Testing::MakeStub(p, dev->Pins.ClkTrg[i], name));
	}
	for (int i = 0; i < 3; ++i) {
		const auto name = std::format("ZcTo{}", i);
		h.Points.emplace_back(name, Sim::Testing::MakeStub(p, dev->Pins.ZcTo[i], name));
	}
	h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<Z80CTC>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("Z80CTC", CreateZ80CTC)
