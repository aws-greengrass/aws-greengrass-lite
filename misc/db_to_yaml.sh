#!/bin/bash

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: $0 <path_to_config.db>"
    echo "Convert Greengrass configuration database to YAML format"
    echo ""
    echo "Arguments:"
    echo "  <path_to_config.db>  Path to the configuration database file"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message and exit"
    exit 0
fi

if [ $# -ne 1 ]; then
    echo "Usage: $0 <path_to_config.db>"
    exit 1
fi

DB_PATH="$1"

if [ ! -f "$DB_PATH" ]; then
    echo "Error: Database file $DB_PATH not found"
    exit 1
fi

# Create a temporary file for processing
TEMP_FILE=$(mktemp)

# Function to process hierarchy recursively
process_node() {
    local parent_id="$1"
    local indent="$2"
    sqlite3 "$DB_PATH" "SELECT k.keyvalue, k.keyid, v.value FROM keyTable k LEFT JOIN relationTable r ON k.keyid = r.keyid LEFT JOIN valueTable v ON k.keyid = v.keyid WHERE r.parentid = $parent_id ORDER BY k.keyid;" | while IFS='|' read -r keyvalue keyid value; do
        child_count=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM relationTable WHERE parentid = $keyid;")

        if [ "$child_count" -gt 0 ]; then
            echo "${indent}${keyvalue}:" >> "$TEMP_FILE"
            process_node "$keyid" "$indent  "
        elif [ -n "$value" ]; then
            clean_value=$(echo "$value" | sed 's/^"//; s/"$//')
            if [[ "$clean_value" =~ ^[0-9]+$ ]]; then
                echo "${indent}${keyvalue}: $clean_value" >> "$TEMP_FILE"
            else
                echo "${indent}${keyvalue}: \"$clean_value\"" >> "$TEMP_FILE"
            fi
        else
            echo "${indent}${keyvalue}: {}" >> "$TEMP_FILE"
        fi
    done
}

# Start output
echo "---" > "$TEMP_FILE"

# Process root nodes
sqlite3 "$DB_PATH" "SELECT k.keyvalue, k.keyid, v.value FROM keyTable k LEFT JOIN relationTable r ON k.keyid = r.keyid LEFT JOIN valueTable v ON k.keyid = v.keyid WHERE r.parentid IS NULL ORDER BY k.keyid;" | while IFS='|' read -r keyvalue keyid value; do
    child_count=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM relationTable WHERE parentid = $keyid;")

    if [ "$child_count" -gt 0 ]; then
        echo "${keyvalue}:" >> "$TEMP_FILE"
        process_node "$keyid" "  "
    elif [ -n "$value" ]; then
        clean_value=$(echo "$value" | sed 's/^"//; s/"$//')
        if [[ "$clean_value" =~ ^[0-9]+$ ]]; then
            echo "${keyvalue}: $clean_value" >> "$TEMP_FILE"
        else
            echo "${keyvalue}: \"$clean_value\"" >> "$TEMP_FILE"
        fi
    else
        echo "${keyvalue}: {}" >> "$TEMP_FILE"
    fi
done

# Output the result and clean up
cat "$TEMP_FILE"
rm "$TEMP_FILE"
