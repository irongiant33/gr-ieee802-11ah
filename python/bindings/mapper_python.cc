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
/* BINDTOOL_HEADER_FILE(mapper.h)                                        */
/* BINDTOOL_HEADER_FILE_HASH(c6f5f974184ef7cdf5cbb985b4e0e0a1)                     */
/***********************************************************************************/

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <ieee802_11ah/mapper.h>
// pydoc.h is automatically generated in the build directory
#include <mapper_pydoc.h>

void bind_mapper(py::module& m)
{

    using mapper    = ::gr::ieee802_11ah::mapper;


    py::class_<mapper, gr::block, gr::basic_block,
        std::shared_ptr<mapper>>(m, "mapper", D(mapper))

        .def(py::init(&mapper::make),
           py::arg("mcs"),
           py::arg("debug") = false,
           D(mapper,make)
        )
        




        
        .def("set_encoding",&mapper::set_encoding,       
            py::arg("mcs"),
            D(mapper,set_encoding)
        )

        ;

    py::enum_<::gr::ieee802_11ah::Encoding>(m,"Encoding")
        .value("BPSK_1_2", ::gr::ieee802_11ah::BPSK_1_2) // 0
        .value("QPSK_1_2", ::gr::ieee802_11ah::QPSK_1_2) // 1
        .value("QPSK_3_4", ::gr::ieee802_11ah::QPSK_3_4) // 2
        .value("QAM16_1_2", ::gr::ieee802_11ah::QAM16_1_2) // 3
        .value("QAM16_3_4", ::gr::ieee802_11ah::QAM16_3_4) // 4
        .value("QAM64_2_3", ::gr::ieee802_11ah::QAM64_2_3) // 5
        .value("QAM64_3_4", ::gr::ieee802_11ah::QAM64_3_4) // 6
        .value("QAM64_5_6", ::gr::ieee802_11ah::QAM64_5_6) // 7
        .value("BPSK_1_2_REP", ::gr::ieee802_11ah::BPSK_1_2_REP) //10
        .export_values()
    ;

    py::implicitly_convertible<int, ::gr::ieee802_11ah::Encoding>();


}








