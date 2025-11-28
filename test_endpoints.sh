#!/usr/bin/env bash
set -uo pipefail

SERVER_URL="http://localhost:10480"

OK=0
FAIL=0

# Colors (only when stdout is a terminal)
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    RESET='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    CYAN=''
    BOLD=''
    RESET=''
fi

print_result() {
    if [[ $1 -eq 0 ]]; then
        echo -e "${GREEN}[OK]   ${RESET}$2"
        OK=$((OK+1))
    else
        echo -e "${RED}[FAIL] ${RESET}$2"
        FAIL=$((FAIL+1))
    fi
}

############################################
# Helper: Test valid locations
############################################
test_valid_location() {
    local location="$1"
    local attempt=0
    local max_attempts=3

    while true; do
        attempt=$((attempt + 1))
        RESPONSE=$(curl -sS --max-time 10 -G --data-urlencode "location=$location" -w "\n%{http_code}" "$SERVER_URL/weather")
        BODY=$(echo "$RESPONSE" | head -n -1)
        CODE=$(echo "$RESPONSE" | tail -n 1)
        # If it's a success, break. If it's a 4xx, it's a permanent failure. If 5xx, retry.
        if [[ "$CODE" == "200" ]]; then
            break
        fi
        if [[ "$CODE" =~ ^4[0-9][0-9]$ ]]; then
            # Not found / bad request - don't retry
            break
        fi
        # 5xx -> retry up to max_attempts
        if [[ "$CODE" =~ ^5[0-9][0-9]$ && $attempt -lt $max_attempts ]]; then
            sleep 1
            continue
        fi
        break
    done

    if [[ "$CODE" != "200" ]]; then
        print_result 1 "Valid location '$location' returned HTTP $CODE"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi

    # Check if JSON contains expected fields
    echo "$BODY" | jq -e '.latitude, .longitude, .current_weather' >/dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        print_result 1 "Location '$location' missing expected JSON fields"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi

    # Validate coordinates are not 0.0 (indicates geocoding failure)
    local lat lon
    lat=$(echo "$BODY" | jq -r '.latitude')
    lon=$(echo "$BODY" | jq -r '.longitude')
    if [[ "$lat" == "0.0" && "$lon" == "0.0" ]]; then
        print_result 1 "Location '$location' returned invalid coordinates (0.0, 0.0) - geocoding likely failed"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi

    print_result 0 "Location '$location' returned valid JSON"
}

############################################
# Helper: Test invalid locations (expect non-200)
############################################
test_invalid_location() {
    local location="$1"

    # Only capture HTTP status for invalid tests; avoid capturing large bodies
    CODE=$(curl -sS --max-time 10 -G --data-urlencode "location=$location" -o /dev/null -w "%{http_code}" "$SERVER_URL/weather")

    if [[ "$CODE" == "200" ]]; then
        print_result 1 "Invalid location '$location' unexpectedly returned HTTP 200"
        # Capture and print body for debugging
        BODY=$(curl -sS --max-time 10 -G --data-urlencode "location=$location" "$SERVER_URL/weather")
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    elif [[ "$CODE" =~ ^5[0-9][0-9]$ ]]; then
        # 5xx indicates a server-side failure, not a not-found condition
        print_result 1 "Invalid location '$location' returned server error HTTP $CODE"
        return
    else
        print_result 0 "Invalid location '$location' returned expected error HTTP $CODE"
    fi
}

############################################
# Helper: Test generic endpoint (expect HTTP 200)
############################################
test_endpoint() {
    local path="$1"
    local desc="$2"
    local expected_content="${3:-}"

    # Ensure path begins with '/'
    if [[ "${path}" != /* ]]; then
        path="/${path}"
    fi

    if [[ -n "$expected_content" ]]; then
        RESPONSE=$(curl -sS --max-time 10 -w "\n%{http_code}" "$SERVER_URL${path}")
        CODE=$(echo "$RESPONSE" | tail -n 1)
        BODY=$(echo "$RESPONSE" | head -n -1)
        if [[ "$CODE" != "200" ]]; then
            print_result 1 "$desc ($path) returned HTTP $CODE"
            return
        fi
        if ! echo "$BODY" | grep -iq "$expected_content"; then
            print_result 1 "$desc ($path) returned HTTP 200 but body does not contain expected text"
            echo -e "${YELLOW}Response body:${RESET} $BODY"
            return
        fi
        print_result 0 "$desc ($path) returned HTTP 200 and contains expected text"
        return
    else
        # For static endpoints with no expected body, just check HTTP status
        CODE=$(curl -sS --max-time 10 -o /dev/null -w "%{http_code}" "$SERVER_URL${path}")
    fi

    if [[ "$CODE" != "200" ]]; then
        print_result 1 "$desc ($path) returned HTTP $CODE"
        # Capture and print body for debugging
        BODY=$(curl -sS --max-time 10 "$SERVER_URL${path}")
        echo -e "${YELLOW}Response body:${RESET} $BODY"
    else
        print_result 0 "$desc ($path) returned HTTP 200"
    fi
}

############################################
# Start tests
############################################
echo -e "${CYAN}${BOLD}=== Testing UB-WeatherServer ===${RESET}"
echo -e "${CYAN}Server: ${RESET}$SERVER_URL"
echo ""

# Preflight: ensure server is reachable (detect curl HTTP 000)
check_server() {
    local code
    code=$(curl -sS -o /dev/null -w "%{http_code}" --connect-timeout 3 "$SERVER_URL/") || true
    if [[ "$code" == "000" ]]; then
        echo -e "${RED}ERROR:${RESET} Cannot connect to ${SERVER_URL} (HTTP 000). Is the server running?"
        exit 2
    fi
}

# Ensure required CLI tools exist
if ! command -v curl >/dev/null 2>&1; then
    echo -e "${RED}ERROR:${RESET} curl is required for this test script"
    exit 2
fi
if ! command -v jq >/dev/null 2>&1; then
    echo -e "${RED}ERROR:${RESET} jq is required for JSON validation in this test script"
    exit 2
fi

check_server

## URL-encoding is handled by `curl --get --data-urlencode` in this script;
## the helper function removed to avoid confusion.

############################################
# Positive tests – legit Swedish cities
############################################
VALID_LOCATIONS=(
    "Stockholm"
    "Göteborg"
    "Malmö"
    "Åre"
    "Örebro"
    "Uppsala"
    "Luleå"
    "Västerås"
    "Torshälla"
)


for loc in "${VALID_LOCATIONS[@]}"; do
    test_valid_location "$loc"
     sleep 0.1
done

############################################
# Case variations
############################################
CASE_VARIANTS=(
    "stockholm"
    "STOCKHOLM"
    "GöTeBoRg"
    "åre"
    "ÅRE"
)

for loc in "${CASE_VARIANTS[@]}"; do
    # Case-insensitive lookup: case/diacritic variants should match
    test_valid_location "$loc"
    sleep 0.1
done

############################################
# Fuzzy / misspellings — these should NOT match the API
# (we expect the server to treat misspellings as not-found / error)
############################################
FUZZY=(
    "Gotheborg"
)

for loc in "${FUZZY[@]}"; do
    # Misspellings should be treated as client errors under exact-match policy
    test_invalid_location "$loc"
    sleep 0.1
done

############################################
# Negative tests – invalid or weird input
############################################
INVALID_LOCATIONS=(
    ""
    "     "
    "!!!!"
    "1234567890"
    "$(head /dev/urandom | tr -dc 'A-Za-z0-9' | head -c 300)"
    "$(printf 'å%.0s' {1..400})"   # Very long Swedish input
)

for loc in "${INVALID_LOCATIONS[@]}"; do
    test_invalid_location "$loc"
     sleep 0.1
done

############################################
# Static endpoint tests (root, index, health, surprise)
############################################
test_endpoint "/" "Root endpoint" "Hello from weather server!"
 sleep 0.1
test_endpoint "/index.html" "Index endpoint" "SERVER MONITOR"
 sleep 0.1
test_endpoint "/health" "Health endpoint" "OK"
 sleep 0.1
test_endpoint "/surprise" "Surprise endpoint"
 sleep 0.1

############################################
# Summary
############################################
echo ""
echo -e "${BOLD}=== Summary ===${RESET}"
echo -e "Passed: ${GREEN}$OK${RESET}"
echo -e "Failed: ${RED}$FAIL${RESET}"

if [[ $FAIL -eq 0 ]]; then
    echo -e "${GREEN}ALL TESTS PASSED${RESET}"
else
    echo -e "${RED}SOME TESTS FAILED${RESET}"
fi

[[ $FAIL -eq 0 ]] && exit 0 || exit 1
