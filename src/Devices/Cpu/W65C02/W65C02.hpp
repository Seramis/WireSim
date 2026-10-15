#include "../../../Simulator/Inspect.hpp"
#include "../../../Simulator/Platform.hpp"
#include "W65C02Impl.hpp"

class W65C02 final : public Sim::Device, public Sim::IRegisterInspectable {
  public:
	Sim::Pin CLK, CLKout, IRQB, NMIB, RESB, RDY, RWB;
	Sim::Bus A, D;

	W65C02(Sim::Platform* p) :
		CLK(Sim::Pin::New(p, "CLK")), CLKout(Sim::Pin::New(p, "CLKout")), IRQB(Sim::Pin::New(p, "IRQB")),
		NMIB(Sim::Pin::New(p, "NMIB")), RESB(Sim::Pin::New(p, "RESB")), RDY(Sim::Pin::New(p, "RDY")),
		RWB(Sim::Pin::New(p, "RWB")), A(Sim::Bus::New(p, 16, "A")), D(Sim::Bus::New(p, 8, "D")),
		_diagnostics(p->GetDiagnostics()) {
		CLK.AddTrigger(this, LogicFlags::Clock, false);
		NMIB.AddTrigger(this, LogicFlags::Nmi, false);
	}

	void DumpState() const {
		_cpu.DumpState(_diagnostics.cpuCycles, _diagnostics.halfCycles);
	}

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		return _cpu.DumpRegisters();
	}

	void Logic(Sim::TriggerMask mask) {
		_cpu.pins.PHI2in = CLK.IsHigh();
		_cpu.pins.IRQBin = IRQB.IsHigh();
		_cpu.pins.NMIBin = NMIB.IsHigh();
		_cpu.pins.RESBin = RESB.IsHigh();
		_cpu.pins.RDYin = RDY.IsHigh();
		_cpu.pins.Din = D.Read<uint8_t>();

		const bool clockChanged = mask.IsSet(LogicFlags::Clock);
		if (clockChanged && _cpu.pins.RWBout && !_cpu.pins.PHI2in && _cpu.pins.RDYin) {
			_diagnostics.Println("CPU sample R {:04x}:{:02x}", _cpu.pins.Aout, _cpu.pins.Din);
		}

		_cpu.Next();
		if (!clockChanged) return;

		CLKout.Write(_cpu.pins.PHI2out);

		if (!_cpu.pins.RWBout) {
			_diagnostics.Println(
				"CPU PHI2={} W {:04x}:{:02x} RESB={} RDY={} IRQB={} NMIB={}",
				_cpu.pins.PHI2in,
				_cpu.pins.Aout,
				_cpu.pins.Dout,
				_cpu.pins.RESBin,
				_cpu.pins.RDYin,
				_cpu.pins.IRQBin,
				_cpu.pins.NMIBin
			);
		} else {
			_diagnostics.Println(
				"CPU PHI2={} R {:04x} Din={:02x} RESB={} RDY={} IRQB={} NMIB={}",
				_cpu.pins.PHI2in,
				_cpu.pins.Aout,
				_cpu.pins.Din,
				_cpu.pins.RESBin,
				_cpu.pins.RDYin,
				_cpu.pins.IRQBin,
				_cpu.pins.NMIBin
			);
		}

		A.Write(_cpu.pins.Aout);

		RWB.Write(_cpu.pins.RWBout);
		if (_cpu.pins.RWBout) {
			D.Clear();
		} else {
			D.Write(_cpu.pins.Dout);
		}
	}

  private:
	enum class LogicFlags : uint8_t {
		Clock = 0,
		Nmi = 1
	};

	W65C02Impl::Cpu _cpu;
	Sim::Diagnostics& _diagnostics;
};