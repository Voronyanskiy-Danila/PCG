#include "TextureUvSettings.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	void StripUtf8Bom(std::string& s)
	{
		if (s.size() >= 3u && static_cast<unsigned char>(s[0]) == 0xEFu
			&& static_cast<unsigned char>(s[1]) == 0xBBu && static_cast<unsigned char>(s[2]) == 0xBFu)
			s.erase(0, 3);
	}

	std::string Trim(std::string s)
	{
		auto a = std::find_if_not(s.begin(), s.end(),
			[](unsigned char c) { return std::isspace(c) != 0; });
		auto b = std::find_if_not(s.rbegin(), s.rend(),
			[](unsigned char c) { return std::isspace(c) != 0; }).base();
		return (a >= b) ? std::string{} : std::string(a, b);
	}

	bool ParseBool(const std::string& vRaw)
	{
		std::string v = Trim(vRaw);
		for (char& c : v)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return v == "1" || v == "true" || v == "yes" || v == "on";
	}

	bool ParsePositiveFloat(std::string v, float* outVal)
	{
		v = Trim(std::move(v));
		for (char& c : v)
			if (c == ',')
				c = '.';
		if (v.empty())
			return false;

		float x = 0.f;
		const char* const first = v.data();
		const char* const last = first + v.size();
		const std::from_chars_result r = std::from_chars(first, last, x);
		if (r.ec != std::errc{} || r.ptr != last || !std::isfinite(x) || x <= 0.f || x > 1e4f)
			return false;
		*outVal = x;
		return true;
	}

	std::string NormalizeKey(std::string key)
	{
		key = Trim(std::move(key));
		for (char& c : key)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		key.erase(std::remove(key.begin(), key.end(), '_'), key.end());
		return key;
	}

	void ApplyIniLines(std::istream& fin, TextureUvSettings& io)
	{
		bool haveGlobalTiling = false;
		float globalTiling = io.TilingRepeatsX;
		bool setU = false;
		bool setV = false;
		float uVal = io.TilingRepeatsX;
		float vVal = io.TilingRepeatsY;

		for (std::string line; std::getline(fin, line); )
		{
			StripUtf8Bom(line);
			const auto hash = line.find('#');
			if (hash != std::string::npos)
				line.erase(hash);
			line = Trim(std::move(line));
			if (line.empty())
				continue;

			const auto eq = line.find('=');
			if (eq == std::string::npos)
				continue;

			const std::string key = NormalizeKey(line.substr(0, eq));
			const std::string val = Trim(line.substr(eq + 1));
			float f = 0.f;

			if (key == "tilingrepeats" || key == "tilingrepeat" || key == "tiling")
			{
				if (ParsePositiveFloat(val, &f))
				{
					globalTiling = f;
					haveGlobalTiling = true;
				}
			}
			else if (key == "tilingrepeatsu" || key == "tilingu")
			{
				if (ParsePositiveFloat(val, &f))
				{
					uVal = f;
					setU = true;
				}
			}
			else if (key == "tilingrepeatsv" || key == "tilingv")
			{
				if (ParsePositiveFloat(val, &f))
				{
					vVal = f;
					setV = true;
				}
			}
			else if (key == "texturemovement" || key == "uvmovement")
			{
				io.TextureMovementEnabled = ParseBool(val);
			}
		}

		if (haveGlobalTiling)
		{
			io.TilingRepeatsX = globalTiling;
			io.TilingRepeatsY = globalTiling;
		}
		if (setU)
			io.TilingRepeatsX = uVal;
		if (setV)
			io.TilingRepeatsY = vVal;
	}
}

bool TextureUvSettings::LoadIni(const wchar_t* pathWide, TextureUvSettings& io)
{
	if (!pathWide || !pathWide[0])
		return false;

	std::ifstream fin{std::filesystem::path(pathWide), std::ios::binary};
	if (!fin)
		return false;

	ApplyIniLines(fin, io);
	return true;
}
