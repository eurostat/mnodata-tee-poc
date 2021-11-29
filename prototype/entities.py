#
# Copyright 2021 European Union
#
# Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
# the European Commission - subsequent versions of the EUPL (the "Licence");
# You may not use this work except in compliance with the Licence.
# You may obtain a copy of the Licence at:
#
# https://joinup.ec.europa.eu/software/page/eupl
#
# Unless required by applicable law or agreed to in writing, software 
# distributed under the Licence is distributed on an "AS IS" basis,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the Licence for the specific language governing permissions and 
# limitations under the Licence.
#

from typing import Dict, Tuple


class Pretty:
    def __str__(self):
        return str(self.__dict__)

# Row in accumulated footprint (S) or intra-period footprint (H_m) tables
class Footprint(Pretty):
    def __init__(self, id, tile_e, tile_n, values):
        self.id = id
        self.tile_e = int(tile_e)
        self.tile_n = int(tile_n)
        # values of periods 0, 1, 2, 3
        self.values = values

    def key(self):
        return (self.id, self.tile_e, self.tile_n)

# Row in quantised footprint (Y_m) table
class QuantisedFootprint(Pretty):
    def __init__(self, id, tile_e, tile_n, values, ref_areas=[], weight=1, rank=None):
        self.id = id
        self.tile_e = int(tile_e)
        self.tile_n = int(tile_n)
        # 0 or 1 for each sub-period (0, 1, 2, 3) indicating if the tile is
        # present in the user's quantised footprint
        self.values = values
        # List of indices of reference areas that have at least one tile in
        # common with this user's tiles. delta_r in the document.
        self.ref_areas = ref_areas
        # Calibration weight
        self.weight = weight
        # Rank in anchor tile (L_m) ordering
        self.rank = rank

# Row in footprint sum table (D)
class TotalFootprint(Pretty):
    def __init__(self, tile_e, tile_n, totals):
        self.tile_e = int(tile_e)
        self.tile_n = int(tile_n)
        self.totals = totals

# Tile coordinates: pair of easting, northing.
Coord = Tuple[int, int]

# Top anchor distribution table (P). Maps easting, northing coordinates to
# number of users for whom this tile is their top anchor.
TopAnchorDistribution = Dict[Coord, int]

# Resident counts table (\ell). Maps easting, northing coordinates to absolute
# number of people in this tile according to census data.
Residents = Dict[Coord, float]

# Functional urban fingerprint (C). Maps pair of (tile coordinates, reference
# area index) to connection strength.
ConnectionStrengths = Dict[Tuple[Coord, int], float]
