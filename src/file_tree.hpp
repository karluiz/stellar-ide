#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace raven {

struct FileTreeEntry {
    std::filesystem::path path;
    bool directory = false;
};

// The entries of one folder as the Files explorer shows them: folders first, then files, each
// group in case-insensitive name order. Hidden entries (a leading dot) are left out. A folder that
// cannot be read yields nothing rather than throwing.
std::vector<FileTreeEntry> list_file_tree(const std::filesystem::path& directory);

enum class FileTreeIcon { Folder, File };

// A small antialiased RGBA icon of `size` × `size` pixels in one colour, drawn inside a margin so a
// tree row can show it at its full row height and the glyph still reads at text size.
std::vector<std::uint8_t> file_tree_icon(FileTreeIcon icon, int size, float r, float g, float b, float a);

} // namespace raven
