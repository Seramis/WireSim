#include "Pin.hpp"
#include "Platform.hpp"

namespace Sim {

Pin Pin::New(Platform* p, std::string name) {
	int pinId = p->_pins.NewId();
	int netId = p->_nets.NewId();

	p->_pins.Names[pinId] = name.empty() ? "Pin_" + std::to_string(pinId) : name;
	p->_pins.NetIds[pinId] = netId;
	p->_nets.PinIds[netId].push_back(pinId);

	// New pins default to FlatDrives slot 0 (a dummy/null entry) until Bake()
	// assigns their real slot.
	if (p->_pins.FlatDriveIndices.size() <= (size_t)pinId) {
		p->_pins.FlatDriveIndices.resize(pinId + 1, 0);
	}
	if (p->_nets.FlatDrives.empty()) {
		p->_nets.FlatDrives.push_back(LogicState::F);
	}

	return {p, pinId};
}

Pin Pin::PullUp(Platform* p, std::string name) {
	auto pin = New(p, name);
	p->Enqueue([pin]() mutable {
		pin.Write(LogicState::Hw);
	});
	return pin;
}

Pin Pin::PullDown(Platform* p, std::string name) {
	auto pin = New(p, name);
	p->Enqueue([pin]() mutable {
		pin.Write(LogicState::Lw);
	});
	return pin;
}

std::string Pin::ToString() const {
	return _platform->_pins.Names[_id] + ":" + Read().ToString();
}

} // namespace Sim
