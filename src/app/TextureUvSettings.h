#pragma once

struct TextureUvSettings
{
	float TilingRepeatsX = 1.08f;
	float TilingRepeatsY = 1.08f;
	bool TextureMovementEnabled = true;

	// При успехе читает INI поверх переданных в io значений. false — файл не найден или пустой ввод.
	static bool LoadIni(const wchar_t* pathWide, TextureUvSettings& io);
};
