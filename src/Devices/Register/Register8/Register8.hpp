#pragma once
#include "../../../Simulator/Platform.hpp"
#include "_gen/VRegister8.h"
#include <memory>
#include <string>

// First-class Verilog device: an 8-bit transparent-latch register
// (Register8.v), with CE/WE/OE control lines and a byte-wide D bus for
// direct wiring into a machine's Chipset.
// Writes latch D onto the Verilog model's d_in while NotWE is asserted;
// reads drive D from the model's d_out while NotOE is asserted.
class Register8 final : public Sim::Device {
  public:
	struct Register8Pins {
		Sim::Bus D;
		Sim::Pin NotCE, NotWE, NotOE;
	};
	Register8Pins Pins;

  private:
	enum class CTRL : uint8_t {
		NotCE = 0,
		NotOE = 1,
		NotWE = 2
	};

	std::unique_ptr<VRegister8> _model;
	Sim::Diagnostics& _diagnostics;
	std::string _name;

  public:
	Register8(Sim::Platform* p, std::string name = "Register8") :
		Pins{
			Sim::Bus::New(p, 8, name + ".D"),
			Sim::Pin::New(p, name + ".NotCE"),
			Sim::Pin::New(p, name + ".NotWE"),
			Sim::Pin::New(p, name + ".NotOE")
		},
		_model(std::make_unique<VRegister8>()), _diagnostics(p->GetDiagnostics()), _name(std::move(name)) {
		Pins.NotCE.AddTrigger(this, CTRL::NotCE);
		Pins.NotOE.AddTrigger(this, CTRL::NotOE);
		Pins.NotWE.AddTrigger(this, CTRL::NotWE, false);
	}

	void Logic(Sim::TriggerMask mask) {
		const auto isEnabled = Pins.NotCE.IsLow();
		if (!isEnabled) {
			Pins.D.Clear();
			return;
		}
		const auto isWrite = isEnabled && Pins.NotWE.IsLow();
		const auto isRead = isEnabled && Pins.NotOE.IsLow() && !isWrite;

		_model->we = isWrite ? 1 : 0;
		_model->d_in = isWrite ? Pins.D.Read<uint8_t>() : 0;
		_model->eval();

		if (isWrite && mask.IsSet(CTRL::NotWE)) {
			_diagnostics.Println("{} write {:02x}", _name, _model->d_out);
		}

		if (isRead) {
			Pins.D.Write(_model->d_out);
		} else {
			Pins.D.Clear(); // High-Z
		}
	}
};
