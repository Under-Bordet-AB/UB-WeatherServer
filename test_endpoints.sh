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
# Helper: Test valid locations using /GetLocation and then /GetWeather
############################################
test_get_weather() {
    local lat="$1"
    local lon="$2"
    RESPONSE=$(curl -sS --max-time 10 -G --data-urlencode "lat=$lat" --data-urlencode "lon=$lon" -w "\n%{http_code}" "$SERVER_URL/GetWeather")
    BODY=$(echo "$RESPONSE" | head -n -1)
    CODE=$(echo "$RESPONSE" | tail -n 1)
    if [[ "$CODE" != "200" ]]; then
        print_result 1 "GetWeather($lat,$lon) returned HTTP $CODE"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi
    echo "$BODY" | jq -e '.current_weather' >/dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        print_result 1 "GetWeather($lat,$lon) missing expected 'current_weather' field"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi
    print_result 0 "GetWeather($lat,$lon) returned valid JSON"
}

test_valid_location() {
    local location="$1"
    RESPONSE=$(curl -sS --max-time 10 -G --data-urlencode "name=$location" -w "\n%{http_code}" "$SERVER_URL/GetLocation")
    BODY=$(echo "$RESPONSE" | head -n -1)
    CODE=$(echo "$RESPONSE" | tail -n 1)
    if [[ "$CODE" != "200" ]]; then
        print_result 1 "GetLocation '$location' returned HTTP $CODE"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi

    # Expect latitude & longitude in the response
    # The backend may return either a single object or an array of matches.
    # Handle both: prefer the first array element if an array is returned.
    if echo "$BODY" | jq -e 'type == "array" and length > 0' >/dev/null 2>&1; then
        lat=$(echo "$BODY" | jq -r '.[0].latitude')
        lon=$(echo "$BODY" | jq -r '.[0].longitude')
    elif echo "$BODY" | jq -e 'has("latitude") and has("longitude")' >/dev/null 2>&1; then
        lat=$(echo "$BODY" | jq -r '.latitude')
        lon=$(echo "$BODY" | jq -r '.longitude')
    else
        print_result 1 "GetLocation '$location' returned unexpected JSON structure"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi
    if [[ -z "$lat" || -z "$lon" ]]; then
        print_result 1 "GetLocation '$location' returned empty coordinates"
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    fi

    # Validate returned name (case-insensitive match). Backend may return
    # capitalised or canonical names; accept any case variant but ensure the
    # semantic match is correct.
    resp_name=$(echo "$BODY" | jq -r 'if type=="array" then (.[0].name // "") elif has("name") then (.name // "") else "" end')
    if [[ -n "$resp_name" ]]; then
        lc_req=$(printf '%s' "$location" | tr '[:upper:]' '[:lower:]')
        lc_resp=$(printf '%s' "$resp_name" | tr '[:upper:]' '[:lower:]')
        if [[ "$lc_req" != "$lc_resp" ]]; then
            print_result 1 "GetLocation '$location' returned name '$resp_name' (case-insensitive mismatch)"
            echo -e "${YELLOW}Response body:${RESET} $BODY"
            return
        fi
    fi

    print_result 0 "GetLocation '$location' returned coordinates: $lat,$lon"
    # Also test GetWeather for these coordinates
    test_get_weather "$lat" "$lon"
}

############################################
# Helper: Test invalid locations (expect non-200)
############################################
test_invalid_location() {
    local location="$1"
    CODE=$(curl -sS --max-time 10 -G --data-urlencode "name=$location" -o /dev/null -w "%{http_code}" "$SERVER_URL/GetLocation")
    if [[ "$CODE" == "200" ]]; then
        print_result 1 "Invalid location '$location' unexpectedly returned HTTP 200"
        BODY=$(curl -sS --max-time 10 -G --data-urlencode "name=$location" "$SERVER_URL/GetLocation")
        echo -e "${YELLOW}Response body:${RESET} $BODY"
        return
    elif [[ "$CODE" =~ ^5[0-9][0-9]$ ]]; then
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
    "Uppsala"
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
# API endpoint smoke tests (updated paths)
############################################
test_endpoint "/GetCities" "GetCities endpoint"
sleep 0.1
test_endpoint "/GetLocation?name=Stockholm&count=1" "GetLocation endpoint"
sleep 0.1
test_endpoint "/GetWeather?lat=59.33&lon=18.07" "GetWeather endpoint"
sleep 0.1
test_endpoint "/GetSurprise" "GetSurprise endpoint"
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
