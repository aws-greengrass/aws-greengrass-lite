#!/bin/bash

# Global variables
DB_PATH=""
OUTPUT_FILE=""

# Show help message
show_help() {
    echo "Usage: $0 <path_to_config.db>"
    echo "Convert Greengrass configuration database to JSON format"
    echo ""
    echo "Arguments:"
    echo "  <path_to_config.db>  Path to the configuration database file"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message and exit"
    echo ""
    echo "Note: Output is unformatted. Use your IDE or online JSON formatters to pretty-print."
}

# Validate input arguments
validate_args() {
    if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
        show_help
        exit 0
    fi

    if [ $# -ne 1 ]; then
        echo "Usage: $0 <path_to_config.db>"
        exit 1
    fi

    if [ ! -f "$1" ]; then
        echo "Error: Database file $1 not found"
        exit 1
    fi
}

# Get child count for a given key ID
get_child_count() {
    local key_id="$1"
    sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM relationTable WHERE parentid = $key_id;"
}

# Get nodes for a given parent ID
get_nodes() {
    local parent_id="$1"
    local query="SELECT k.keyvalue, k.keyid, v.value FROM keyTable k
                 LEFT JOIN relationTable r ON k.keyid = r.keyid
                 LEFT JOIN valueTable v ON k.keyid = v.keyid
                 WHERE r.parentid $parent_id ORDER BY k.keyid;"
    sqlite3 "$DB_PATH" "$query"
}

# Format value based on its type
format_value() {
    local value="$1"
    local clean_value

    clean_value=$(echo "$value" | sed 's/^"//; s/"$//')

    # Check if it's a number
    if [[ "$clean_value" =~ ^[0-9]+$ ]]; then
        echo "$clean_value"
    # Check if it's already JSON (array or object)
    elif [[ "$clean_value" =~ ^\[.*\]$ ]] || [[ "$clean_value" =~ ^\{.*\}$ ]]; then
        echo "$clean_value"
    # Otherwise, treat as string
    else
        echo "\"$clean_value\""
    fi
}

# Write JSON key-value pair
write_json_pair() {
    local indent="$1"
    local key="$2"
    local value="$3"
    local is_object="$4"

    if [ "$is_object" = "true" ]; then
        echo -n "${indent}\"${key}\": {" >> "$OUTPUT_FILE"
    else
        echo -n "${indent}\"${key}\": $value" >> "$OUTPUT_FILE"
    fi
}

# Add comma separator if not first item
add_comma_if_needed() {
    local is_first="$1"
    if [ "$is_first" = "false" ]; then
        echo "," >> "$OUTPUT_FILE"
    fi
}

# Process database nodes recursively
process_nodes() {
    local parent_id="$1"
    local indent="$2"
    local is_first=true

    while IFS='|' read -r key_name key_id value; do
        add_comma_if_needed "$is_first"
        is_first=false

        local child_count
        child_count=$(get_child_count "$key_id")

        if [ "$child_count" -gt 0 ]; then
            # Has children - create object
            write_json_pair "$indent" "$key_name" "" "true"
            process_nodes "= $key_id" "$indent  "
            echo -n "${indent}}" >> "$OUTPUT_FILE"
        elif [ -n "$value" ]; then
            # Has value - format and write
            local formatted_value
            formatted_value=$(format_value "$value")
            write_json_pair "$indent" "$key_name" "$formatted_value" "false"
        else
            # Empty object
            write_json_pair "$indent" "$key_name" "{}" "false"
        fi
    done < <(get_nodes "$parent_id")
}

# Main function
main() {
    validate_args "$@"

    DB_PATH="$1"
    OUTPUT_FILE=$(mktemp)

    # Start JSON output
    echo "{" > "$OUTPUT_FILE"

    # Process root nodes (where parentid IS NULL)
    process_nodes "IS NULL" "  "

    # End JSON output
    echo "" >> "$OUTPUT_FILE"
    echo "}" >> "$OUTPUT_FILE"

    # Output result and cleanup
    cat "$OUTPUT_FILE"
    rm "$OUTPUT_FILE"
}

# Run main function with all arguments
main "$@"
