# Copyright 2023 Ryan Volz
#
# This file is part of gr-ieee802_11ah
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

install(
    FILES halow_loopback.grc
          halow_phy_hier.grc
          halow_rx.grc
          halow_transceiver.grc
          halow_tx.grc
    DESTINATION ${GR_PKG_DATA_DIR}/examples)
