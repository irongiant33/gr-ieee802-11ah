/*
 * Copyright 2021 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/***********************************************************************************/
/* This file is automatically generated using bindtool and can be manually edited  */
/* The following lines can be configured to regenerate this file during cmake      */
/* If manual edits are made, the following tags should be modified accordingly.    */
/* BINDTOOL_GEN_AUTOMATIC(0)                                                       */
/* BINDTOOL_USE_PYGCCXML(0)                                                        */
/* BINDTOOL_HEADER_FILE(ether_encap.h)                                        */
/* BINDTOOL_HEADER_FILE_HASH(8efbdf21a943b4e223d76153049349f1)                     */
/***********************************************************************************/

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <ieee802_11ah/ether_encap.h>
// pydoc.h is automatically generated in the build directory
#include <ether_encap_pydoc.h>

void bind_ether_encap(py::module& m)
{

    using ether_encap    = ::gr::ieee802_11ah::ether_encap;


    py::class_<ether_encap, gr::block, gr::basic_block,
        std::shared_ptr<ether_encap>>(m, "ether_encap", D(ether_encap))

        .def(py::init(&ether_encap::make),
           py::arg("debug"),
           D(ether_encap,make)
        )
        



        ;




}








