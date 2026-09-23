"""Per-dataset code generators.

Each module here renders ONE data/*.json file into ONE src/generated/*.cpp table
and exposes a TARGETS mapping of {output path: render function}. embed_content.py
discovers them automatically, so adding a dataset means adding a file rather than
editing a shared one -- which is what lets separate datasets be worked on at once.
"""
