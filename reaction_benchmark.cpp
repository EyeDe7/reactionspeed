#include "app.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    fs::path exe_dir = fs::current_path();
    std::string self_path = crypto::getSelfExecutablePath((argc > 0) ? argv[0] : nullptr);
    if (!self_path.empty()) {
        std::error_code ec;
        fs::path p = fs::canonical(self_path, ec);
        if (!ec && p.has_parent_path()) {
            exe_dir = p.parent_path();
        }
    }

    fs::path history_file = exe_dir / "reaction_history.csv";
    fs::path config_file = exe_dir / "reaction_config.ini";

    std::string self_sha256 = crypto::computeSelfBinarySha256((argc > 0) ? argv[0] : nullptr);

    HistoryManager history(history_file);
    net::ClientConfig cfg;
    cfg.loadOrInit(config_file);
    net::FriendsScoreClient net_client(cfg, config_file, self_sha256);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--history" || arg == "-h") {
            history.printSummaryToTerminal();
            return 0;
        } else if (arg == "--clear") {
            history.clear();
            std::cout << "Cleared reaction time history: " << history_file.string() << "\n";
            return 0;
        } else if (arg == "--hash") {
            std::cout << self_sha256 << "\n";
            return 0;
        } else if (arg == "--key") {
            std::cout << "Public Key:   " << cfg.public_key_hex << "\n"
                      << "Private Seed: " << cfg.private_seed_hex << "\n";
            return 0;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [--history] [--clear] [--hash] [--key]\n"
                      << "  (no args)   Launch the Reaction Speed Test GUI\n"
                      << "  --history   Print Top 3, visual chart, and full history to terminal\n"
                      << "  --clear     Clear saved reaction time history\n"
                      << "  --hash      Print SHA-256 hash of this binary for server allowlist\n"
                      << "  --key       Print your Ed25519 Public & Private Key hex for website auditing\n";
            return 0;
        }
    }

    ReactionBenchmarkApp app(history, cfg, net_client);
    if (!app.initPlatform()) {
        return 1;
    }

    app.run();
    history.printSummaryToTerminal();
    return 0;
}
