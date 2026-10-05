#!/usr/bin/env python3
"""Runs the real-editor simulations against novus-lsp: python3 test/lsp/tools/editor_sim/run.py [filter...]
NOVUS_LSP_BIN selects the server binary (default build/novus-lsp)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness
import scen_flow  # noqa: F401
for extra in ("scen_negotiation", "scen_serverreq", "scen_lifecycle", "scen_documents", "scen_robust", "scen_shapes", "scen_typing", "scen_known_defects"):
    try:
        __import__(extra)
    except ModuleNotFoundError as error:
        if error.name != extra:
            raise

outcomes = harness.run_all(sys.argv[1:])
failed = [o for o in outcomes if o.failures]
known = sorted({entry.split(":")[0] for o in outcomes for entry in o.known})
print("%d scenarios, %d failed, %d checks, %d known defects reproduce (%s; see known_defects.txt)"
      % (len(outcomes), len(failed), sum(o.checks for o in outcomes), len(known), ", ".join(known)))
sys.exit(1 if failed else 0)
