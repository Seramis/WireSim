#include "SdCard.hpp"
#include "../../../../Testing/HarnessCore.hpp"
#include <filesystem>

namespace {

Sim::Testing::DeviceHandle CreateSdCard(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	// `path=<file>` is the backing file (required), `size=<bytes>` the card
	// capacity (required). `fresh=1` deletes any existing file first, so the
	// card starts zero-filled.
	if (!args.contains("path") || !args.contains("size")) throw Sim::Testing::HarnessError("SdCard needs path=<file> and size=<bytes>");
	const auto& path = args.at("path").AsString();
	if (args.contains("fresh") && args.at("fresh").AsInt() != 0) std::filesystem::remove(path);

	auto dev = std::make_unique<SdCard>(&p, path, std::stoull(args.at("size").AsString(), nullptr, 0));

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("NotCS", Sim::Testing::MakeStub(p, dev->Pins.NotCS, "NotCS"));
	h.Points.emplace_back("SCLK", Sim::Testing::MakeStub(p, dev->Pins.SCLK, "SCLK"));
	h.Points.emplace_back("MOSI", Sim::Testing::MakeStub(p, dev->Pins.MOSI, "MOSI"));
	h.Points.emplace_back("MISO", Sim::Testing::MakeStub(p, dev->Pins.MISO, "MISO"));
	h.Registers = dev.get();
	h.Memory = dev.get();
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<SdCard>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("SdCard", CreateSdCard)
