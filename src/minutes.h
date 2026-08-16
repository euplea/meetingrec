#pragma once

#include <string>

struct MinutesOptions {
    std::string transcriptText;
    std::string title = "Minuta riunione";
    std::string date;       // YYYY-MM-DD (empty => today)
    std::string attendees;  // comma separated
    std::string outputPath = "minuta.md";
};

// Turns a raw transcript into a Markdown meeting minute.
bool writeMinutes(const MinutesOptions& opts, std::string& error);

// Turns a raw transcript into an OpenDocument Text (.odt) meeting minute.
bool writeMinutesOdt(const MinutesOptions& opts, std::string& error);
