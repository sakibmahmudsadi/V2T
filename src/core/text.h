// Text post-processing rules.
//
// Pure functions, no OS calls: the same code runs in the controller and the
// worker, and it is what the Linux port will reuse unchanged.
#pragma once

#include <string>
#include <vector>

namespace pt {

// The filler list this app ships with: Handy's universal set plus its
// English set. The model is English-only, so no language gating is applied.
std::vector<std::string> default_filler_words();

// Removes whole-word filler tokens (case-insensitive). A trailing comma or
// period that belongs to the removed token goes with it. Other punctuation
// and the original word order are preserved.
std::string remove_filler_words(const std::string& text,
                                const std::vector<std::string>& fillers);

// Collapses runs of whitespace into single spaces and trims the ends.
std::string tidy_whitespace(const std::string& text);

// Appends a single trailing space unless the text is empty or already ends
// with whitespace.
std::string append_trailing_space(const std::string& text);

}  // namespace pt
