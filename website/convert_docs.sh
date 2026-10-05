#!/bin/bash
# Convert all MDX files from website/src/content to website-nv/content/*.md

SOURCE="../website/src/content"
TARGET="content"

# List all MDX files (top-level and subdirs)
for mdx in $(find $SOURCE -name "*.mdx" -type f | sort); do
    base=$(basename "$mdx" .mdx)
    
    # Skip if already converted
    if [ -f "$TARGET/$base.md" ]; then
        echo "Skipping $base (already exists)"
        continue
    fi
    
    echo "Converting $base..."
    
    # Read MDX, convert Callout components to :::note syntax
    cat "$mdx" | \
        sed -E 's/<Callout type="note">/:::note/g' | \
        sed -E 's/<Callout type="tip">/:::tip/g' | \
        sed -E 's/<Callout type="warning">/:::warning/g' | \
        sed -E 's/<\/Callout>/:::/g' | \
        sed -E 's/\{.*\}//g' > "$TARGET/$base.md"
    
    wc -l "$TARGET/$base.md"
done
