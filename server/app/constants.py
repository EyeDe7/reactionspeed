"""Protocol constants and anti-cheat thresholds shared across services."""

GENESIS_HASH = "0" * 64

# --- Canonical payload --------------------------------------------------------
CANONICAL_VERSION = "v1"
CANONICAL_FIELD_COUNT = 10

# --- Game shape ---------------------------------------------------------------
ROUNDS_PER_SET = 5
NICKNAME_MIN_LEN = 2
NICKNAME_MAX_LEN = 24

# --- Challenge tickets --------------------------------------------------------
CHALLENGE_TTL_MS = 120_000
CHALLENGE_WAIT_MIN_MS = 1500
CHALLENGE_WAIT_SPREAD_MS = 3000  # delays are drawn from [MIN, MIN + SPREAD]
CHALLENGE_PRUNE_GRACE_MS = 600_000  # prune expired tickets older than this on each issue
CHALLENGE_PRUNE_STARTUP_GRACE_MS = 3_600_000
MAX_CLOCK_SKEW_S = 300

# --- Anti-cheat ---------------------------------------------------------------
MIN_REACTION_MS = 25.0  # hardware / bot floor
MAX_REACTION_MS = 2000.0
FAST_ROUND_THRESHOLD_MS = 100.0
MAX_FAST_ROUNDS = 2
MIN_HUMAN_AVERAGE_MS = 110.0
MIN_ROUND_STDEV_MS = 0.05
ELAPSED_TOLERANCE_MS = 200.0
FLOAT_TOLERANCE_MS = 0.01

# --- Pagination ---------------------------------------------------------------
DEFAULT_PER_PAGE = 10
MAX_PER_PAGE = 100
DASHBOARD_CHAIN_BLOCKS = 30

# --- Client binaries ----------------------------------------------------------
PLATFORM_WINDOWS = "windows"
PLATFORM_LINUX = "linux"
