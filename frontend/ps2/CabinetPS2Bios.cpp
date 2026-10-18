// See CabinetPS2Bios.h.

#include "CabinetPS2Bios.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

#include "common/FileSystem.h"
#include "common/Path.h"

#include "pcsx2/PrecompiledHeader.h"
#include "pcsx2/ps2/BiosTools.h"

namespace
{
	struct Bios
	{
		std::string name; // the file's name inside the folder
		std::string zone; // PCSX2's reading of its region: "USA", "Europe", "Japan", ...
		u32 version = 0;  // major << 8 | minor, as IsBIOS gives it
	};

	// The region a disc code names. The third letter of the code's prefix is
	// the region in every one of Sony's: SLUS and SCUS are U, SLES, SCES and
	// the European demo discs' SLED and SCED are E, SLPS, SLPM and SCPS are P.
	// Empty for anything else, which goes to step 2.
	std::string RegionOf(const std::string& disc_code)
	{
		if (disc_code.size() < 4)
			return {};
		const auto up = [&](size_t i) { return static_cast<char>(std::toupper(static_cast<unsigned char>(disc_code[i]))); };
		if (up(0) != 'S' || (up(1) != 'L' && up(1) != 'C'))
			return {};
		switch (up(2))
		{
			case 'U': return "USA";
			case 'E': return "Europe";
			case 'P': return "Japan";
			default: return {};
		}
	}

	// The newest BIOS of a region, or of any region when `zone` is empty.
	// `all` is sorted by name, so two of the same version give the same one
	// every time.
	const Bios* Newest(const std::vector<Bios>& all, const std::string& zone)
	{
		const Bios* best = nullptr;
		for (const Bios& b : all)
		{
			if ((zone.empty() || b.zone == zone) && (!best || b.version > best->version))
				best = &b;
		}
		return best;
	}
} // namespace

std::string CabinetPS2::ChooseBios(const std::string& bios_dir, const std::string& disc_code)
{
	// The files PCSX2 itself would look at (FindBiosImage): 4 to 8 MB, in the
	// folder and not below it, and a PS2 BIOS by their contents. Every other
	// system's firmware shares this folder, and none of it passes.
	std::vector<Bios> all;
	FileSystem::FindResultsArray results;
	if (FileSystem::FindFiles(bios_dir.c_str(), "*", FILESYSTEM_FIND_FILES, &results))
	{
		for (const FILESYSTEM_FIND_DATA& fd : results)
		{
			if (fd.Size < 4 * 1024 * 1024 || fd.Size > 8 * 1024 * 1024)
				continue;
			u32 version = 0, region = 0;
			std::string description, zone;
			if (IsBIOS(fd.FileName.c_str(), version, description, region, zone))
				all.push_back({std::string(Path::GetFileName(fd.FileName)), zone, version});
		}
	}
	std::sort(all.begin(), all.end(), [](const Bios& a, const Bios& b) { return a.name < b.name; });

	if (all.empty())
	{
		std::fprintf(stderr, "[ps2] no PS2 BIOS in %s\n", bios_dir.c_str());
		return {};
	}

	const std::string want = RegionOf(disc_code);
	// Why the game's own region did not decide it, for the line below.
	const std::string missing = !want.empty() ? "no " + want + " BIOS for " + disc_code :
								disc_code.empty()  ? std::string("RomM has no disc code for this game") :
													 disc_code + " names no region";
	const Bios* pick = nullptr;
	std::string why;
	if (!want.empty() && (pick = Newest(all, want)))
		why = disc_code + " is " + want;
	else if ((pick = Newest(all, "USA")))
		why = missing + ", so the newest USA one";
	else
	{
		pick = Newest(all, {});
		why = missing + (want == "USA" ? "" : " and no USA one") + ", so the newest there is";
	}

	std::fprintf(stderr, "[ps2] BIOS %s (%s v%02u.%02u, %zu found): %s\n", pick->name.c_str(),
		pick->zone.c_str(), pick->version >> 8, pick->version & 0xff, all.size(), why.c_str());
	return pick->name;
}
