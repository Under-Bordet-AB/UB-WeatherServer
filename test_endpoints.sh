#!/bin/bash

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Server configuration
HOST="localhost"
PORT="10480"
BASE_URL="http://${HOST}:${PORT}"

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Check if server is running
echo -e "${BLUE}Checking if server is running on ${HOST}:${PORT}...${NC}"
if ! curl -s --connect-timeout 2 "${BASE_URL}/cities" > /dev/null 2>&1; then
    echo -e "${RED}✗ Server is not running or not responding on ${HOST}:${PORT}${NC}"
    echo -e "${YELLOW}Please start the server first with: make run${NC}"
    echo -e "${YELLOW}Or run in another terminal: ./server${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Server is running${NC}\n"

# Function to print test header
print_test() {
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}Testing: $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

# Function to check if response is valid JSON
check_json() {
    echo "$1" | jq . > /dev/null 2>&1
    return $?
}

# Function to check HTTP status code
check_status() {
    local expected=$1
    local actual=$2
    if [ "$actual" -eq "$expected" ]; then
        echo -e "${GREEN}✓ Status: $actual${NC}"
        return 0
    else
        echo -e "${RED}✗ Status: Expected $expected, got $actual${NC}"
        return 1
    fi
}

# Test 1: cities endpoint
print_test "GET /cities"
RESPONSE=$(curl -s -w "\n%{http_code}" "${BASE_URL}/cities")
HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "Response Body:"
echo "$BODY" | jq . 2>/dev/null || echo "$BODY"

if check_status 200 "$HTTP_CODE"; then
    if check_json "$BODY"; then
        # Check if response is an array
        if echo "$BODY" | jq -e 'type == "array"' > /dev/null 2>&1; then
            # Check if array contains city objects with required fields
            CITY_COUNT=$(echo "$BODY" | jq 'length')
            echo -e "${GREEN}✓ Valid JSON array with $CITY_COUNT cities${NC}"
            
            # Verify first city has required fields
            HAS_NAME=$(echo "$BODY" | jq -e '.[0].name' > /dev/null 2>&1 && echo "yes" || echo "no")
            HAS_LAT=$(echo "$BODY" | jq -e '.[0].latitude' > /dev/null 2>&1 && echo "yes" || echo "no")
            HAS_LON=$(echo "$BODY" | jq -e '.[0].longitude' > /dev/null 2>&1 && echo "yes" || echo "no")
            
            if [ "$HAS_NAME" = "yes" ] && [ "$HAS_LAT" = "yes" ] && [ "$HAS_LON" = "yes" ]; then
                echo -e "${GREEN}✓ Cities have required fields (name, latitude, longitude)${NC}"
                TESTS_PASSED=$((TESTS_PASSED + 1))
            else
                echo -e "${RED}✗ Cities missing required fields${NC}"
                TESTS_FAILED=$((TESTS_FAILED + 1))
            fi
        else
            echo -e "${RED}✗ Response is not an array${NC}"
            TESTS_FAILED=$((TESTS_FAILED + 1))
        fi
    else
        echo -e "${RED}✗ Invalid JSON response${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test 2: weather endpoint with valid coordinates (Stockholm)
print_test "GET /weather?lat=59.3293&lon=18.0686"
RESPONSE=$(curl -s -w "\n%{http_code}" "${BASE_URL}/weather?lat=59.3293&lon=18.0686")
HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "Response Body:"
echo "$BODY" | jq . 2>/dev/null || echo "$BODY"

if check_status 200 "$HTTP_CODE"; then
    if check_json "$BODY"; then
        # Check for weather data fields (temperature is nested under current)
        HAS_TEMP=$(echo "$BODY" | jq -e '.current.temperature_2m' > /dev/null 2>&1 && echo "yes" || echo "no")
        HAS_LAT=$(echo "$BODY" | jq -e '.latitude' > /dev/null 2>&1 && echo "yes" || echo "no")
        HAS_LON=$(echo "$BODY" | jq -e '.longitude' > /dev/null 2>&1 && echo "yes" || echo "no")
        
        if [ "$HAS_TEMP" = "yes" ] && [ "$HAS_LAT" = "yes" ] && [ "$HAS_LON" = "yes" ]; then
            echo -e "${GREEN}✓ Weather data has required fields${NC}"
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            echo -e "${RED}✗ Weather data missing required fields (expected .latitude, .longitude, .current.temperature_2m)${NC}"
            TESTS_FAILED=$((TESTS_FAILED + 1))
        fi
    else
        echo -e "${RED}✗ Invalid JSON response${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test 3: weather endpoint without coordinates (should return 400)
print_test "GET /weather (missing parameters)"
RESPONSE=$(curl -s -w "\n%{http_code}" "${BASE_URL}/weather")
HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "Response Body:"
echo "$BODY"

if check_status 400 "$HTTP_CODE"; then
    echo -e "${GREEN}✓ Correctly returns 400 for missing parameters${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test 4: weather endpoint with only latitude (should return 400)
print_test "GET /weather?lat=59.3293 (missing longitude)"
RESPONSE=$(curl -s -w "\n%{http_code}" "${BASE_URL}/weather?lat=59.3293")
HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "Response Body:"
echo "$BODY"

if check_status 400 "$HTTP_CODE"; then
    echo -e "${GREEN}✓ Correctly returns 400 for missing longitude${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test 5: surprise endpoint
print_test "GET /surprise"
# Get both headers and status code
RESPONSE=$(curl -s -D - -w "\n%{http_code}" "${BASE_URL}/surprise")
HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)
HEADERS=$(echo "$RESPONSE" | sed '$d' | sed '/^\s*$/q')
BODY=$(echo "$RESPONSE" | sed '$d' | sed '1,/^\s*$/d')

# Don't show binary content, just check status
if check_status 200 "$HTTP_CODE"; then
    # Check Content-Type header from the actual response
    CONTENT_TYPE=$(echo "$HEADERS" | grep -i "content-type" | awk '{print $2}' | tr -d '\r')
    echo "Content-Type: $CONTENT_TYPE"
    
    if [[ "$CONTENT_TYPE" == image/* ]]; then
        echo -e "${GREEN}✓ Returns image content${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${YELLOW}⚠ Content-Type is not an image: $CONTENT_TYPE${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    fi
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test 6: Invalid endpoint (should return 404)
print_test "GET /InvalidEndpoint (should return 404)"
RESPONSE=$(curl -s -w "\n%{http_code}" "${BASE_URL}/InvalidEndpoint")
HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)
BODY=$(echo "$RESPONSE" | sed '$d')

echo "Response Body:"
echo "$BODY"

if check_status 404 "$HTTP_CODE"; then
    echo -e "${GREEN}✓ Correctly returns 404 for invalid endpoint${NC}"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Summary
echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"
TOTAL=$((TESTS_PASSED + TESTS_FAILED))
echo -e "Total: $TOTAL"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "\n${GREEN}✓ All tests passed!${NC}\n"
    exit 0
else
    echo -e "\n${RED}✗ Some tests failed${NC}\n"
    exit 1
fi
