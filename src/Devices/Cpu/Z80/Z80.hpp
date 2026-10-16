#pragma once
#include "../../../Simulator/Inspect.hpp"
#include "../../../Simulator/Platform.hpp"
#include "Z80Impl.hpp"

// Zilog Z80 CPU, driven by CLK (single-phase). See Z80.md.
class Z80 final : public Sim::Device, public Sim::IRegisterInspectable {
  public:
	struct Z80Pins {
		Sim::Bus A, D;
		Sim::Pin NotM1, NotMREQ, NotIORQ, NotRD, NotWR, NotRFSH, NotHALT, NotWAIT, NotINT, NotNMI, NotRESET, NotBUSREQ,
			NotBUSACK, CLK;

		Z80Pins(Sim::Platform* p) :
			A(Sim::Bus::New(p, 16, "A")), D(Sim::Bus::New(p, 8, "D")), NotM1(Sim::Pin::New(p, "NotM1")),
			NotMREQ(Sim::Pin::New(p, "NotMREQ")), NotIORQ(Sim::Pin::New(p, "NotIORQ")),
			NotRD(Sim::Pin::New(p, "NotRD")), NotWR(Sim::Pin::New(p, "NotWR")), NotRFSH(Sim::Pin::New(p, "NotRFSH")),
			NotHALT(Sim::Pin::New(p, "NotHALT")), NotWAIT(Sim::Pin::New(p, "NotWAIT")),
			NotINT(Sim::Pin::New(p, "NotINT")), NotNMI(Sim::Pin::New(p, "NotNMI")),
			NotRESET(Sim::Pin::New(p, "NotRESET")), NotBUSREQ(Sim::Pin::New(p, "NotBUSREQ")),
			NotBUSACK(Sim::Pin::New(p, "NotBUSACK")), CLK(Sim::Pin::New(p, "CLK")) {}
	};
	Z80Pins Pins;

	Z80(Sim::Platform* p) : Pins(p), _diagnostics(p->GetDiagnostics()) {
		Pins.CLK.AddTrigger(this, Trig::Clock, false);
		Pins.NotNMI.AddTrigger(this, Trig::Nmi, false);
	}

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		return _cpu.DumpRegisters();
	}

	void Logic(Sim::TriggerMask) {
		// Active-low inputs read as inactive while nothing drives them.
		auto& in = _cpu.pins;
		in.CLK = Pins.CLK.IsHigh();
		in.WAIT = !Pins.NotWAIT.IsLow();
		in.INT = !Pins.NotINT.IsLow();
		in.NMI = !Pins.NotNMI.IsLow();
		in.RESET = !Pins.NotRESET.IsLow();
		in.BUSREQ = !Pins.NotBUSREQ.IsLow();
		in.Din = Pins.D.Read<uint8_t>();

		if (!_cpu.Next()) return;

		const auto& out = _cpu.pins;
		if (out.aOe) Pins.A.Write(out.Aout);
		else Pins.A.Clear();
		if (out.dOe) Pins.D.Write(out.Dout);
		else Pins.D.Clear();

		const Sim::LogicState released = Sim::LogicState::F;
		auto ctrl = [&](Sim::Pin& pin, bool level) { pin.Write(out.ctrlOe ? Sim::LogicState(level) : released); };
		ctrl(Pins.NotMREQ, out.MREQ);
		ctrl(Pins.NotIORQ, out.IORQ);
		ctrl(Pins.NotRD, out.RD);
		ctrl(Pins.NotWR, out.WR);
		ctrl(Pins.NotRFSH, out.RFSH);
		Pins.NotM1.Write(out.M1);
		Pins.NotHALT.Write(out.HALT);
		Pins.NotBUSACK.Write(out.BUSACK);

		_diagnostics.Println(
			"Z80 CLK={} A={:04x}{} D={:02x}{} M1={} MREQ={} IORQ={} RD={} WR={} RFSH={} HALT={} BUSACK={}",
			in.CLK,
			out.Aout,
			out.aOe ? "" : "(z)",
			out.Dout,
			out.dOe ? "" : "(z)",
			out.M1,
			out.MREQ,
			out.IORQ,
			out.RD,
			out.WR,
			out.RFSH,
			out.HALT,
			out.BUSACK
		);
	}

  private:
	enum class Trig : uint8_t {
		Clock = 0,
		Nmi = 1
	};

	Z80Impl::Cpu _cpu;
	Sim::Diagnostics& _diagnostics;
};
