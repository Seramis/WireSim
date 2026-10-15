#include "Hd44780.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

Sim::Testing::DeviceHandle CreateHd44780(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	const std::string variantArg = args.contains("variant") ? args.at("variant").AsString() : "1602";
	const std::string colorArg = args.contains("colorScheme") ? args.at("colorScheme").AsString() : "green";
	const std::string name = args.contains("name") ? args.at("name").AsString() : "HD44780";

	const auto variant = variantArg == "2004" ? Hd44780Variant::Lcd2004 : Hd44780Variant::Lcd1602;
	const auto colorScheme = colorArg == "blue" ? Hd44780ColorScheme::BlueWhite : Hd44780ColorScheme::GreenDark;

	auto dev = std::make_unique<Hd44780>(&p, variant, colorScheme, name);
	// Harness convention for a UI device (see SdlUiTestHooks.md): open the
	// window unconditionally here, not via a script verb. Non-blocking --
	// Hd44780's own CaptureScreenshot()/Click() already wait for it to
	// actually be open -- and invisible by default (DeviceHarness defaults
	// SDL_VIDEODRIVER to "dummy"; see HarnessMain.cpp), so this costs
	// nothing for a script that never calls `screenshot`.
	dev->OpenWindow();

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("DB", Sim::Testing::MakeStub(p, dev->Pins.DB, "DB"));
	h.Points.emplace_back("RS", Sim::Testing::MakeStub(p, dev->Pins.RS, "RS"));
	h.Points.emplace_back("RW", Sim::Testing::MakeStub(p, dev->Pins.RW, "RW"));
	h.Points.emplace_back("E", Sim::Testing::MakeStub(p, dev->Pins.E, "E"));
	h.Points.emplace_back("BL", Sim::Testing::MakeStub(p, dev->Pins.BL, "BL"));
	h.Ui = dev.get();
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<Hd44780>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("Hd44780", CreateHd44780)
