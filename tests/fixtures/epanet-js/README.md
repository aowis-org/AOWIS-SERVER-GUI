# Synthetic epanet-js interoperability fixtures

These files are original AOWIS test fixtures created from scratch for automated interoperability testing. They are not copied from, transformed from, geometrically shifted from, or otherwise derived from epanet-js demo networks or third-party utility datasets.

- `synthetic-metric.ejsdb` and `synthetic-metric.inp` describe the same small metric hydraulic network. The EJSDB version additionally exercises epanet-js customer points and its embedded age-dependent pipe-material library. Customer demands are aggregated onto their assigned junctions in the paired INP because EPANET INP has no customer-point entity.
- `synthetic-us-customary.ejsdb` and `synthetic-us-customary.inp` describe the same small US-customary hydraulic network and exercise ft/in/gal/min/psi conversion, raw EPANET-style simple controls, and a raw EPANET rule with SYSTEM TIME, node-pressure, THEN/ELSE FCV settings, and priority. Its controlled pipe starts closed while the tank starts below the low-level threshold, so the raw simple control actively opens the pipe at simulation start. Customer demands are likewise aggregated onto their assigned junctions in the paired INP.

The networks, identifiers, coordinates, hydraulic values, material names, demand values, and topology were authored specifically for the AOWIS test suite. They are distributed under the same Apache-2.0 license as AOWIS-SERVER-GUI.
