//
// Copyright 2016 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/rfnoc/dma_fifo_block_ctrl.hpp>
#include <uhd/convert.hpp>
#include <uhd/utils/log.hpp>
#include <uhd/types/wb_iface.hpp>
#include <uhdlib/rfnoc/wb_iface_adapter.hpp>
#include <uhdlib/usrp/cores/dma_fifo_core_3000.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/format.hpp>

using namespace uhd;
using namespace uhd::rfnoc;

class dma_fifo_block_ctrl_impl : public dma_fifo_block_ctrl
{
public:
    static const uint32_t DEFAULT_SIZE = 32*1024*1024;

    UHD_RFNOC_BLOCK_CONSTRUCTOR(dma_fifo_block_ctrl)
    {
        _perifs.resize(get_input_ports().size());
        for(size_t i = 0; i < _perifs.size(); i++) {
            _perifs[i].ctrl = boost::make_shared<wb_iface_adapter>(
                // poke32 functor
                boost::bind(
                    static_cast< void (block_ctrl_base::*)(const uint32_t, const uint32_t, const size_t) >(&block_ctrl_base::sr_write),
                    this, _1, _2, i
                ),
                // peek32 functor
                boost::bind(
                    static_cast< uint32_t (block_ctrl_base::*)(const uint32_t, const size_t) >(&block_ctrl_base::user_reg_read32),
                    this,
                    _1, i
                ),
                // peek64 functor
                boost::bind(
                    static_cast< uint64_t (block_ctrl_base::*)(const uint32_t, const size_t) >(&block_ctrl_base::user_reg_read64),
                    this,
                    _1, i
                )
            );
            static const uint32_t USER_SR_BASE = 128*4;
            static const uint32_t USER_RB_BASE = 0;     //Don't care
            _perifs[i].base_addr = DEFAULT_SIZE*i;
            _perifs[i].depth = DEFAULT_SIZE;
            _perifs[i].core = dma_fifo_core_3000::make(_perifs[i].ctrl, USER_SR_BASE, USER_RB_BASE);
            _perifs[i].core->resize(_perifs[i].base_addr, _perifs[i].depth);
            UHD_LOGGER_INFO("RFNOC DMA FIFO") << boost::format("Running BIST for FIFO %d... ") % i;
            if (_perifs[i].core->ext_bist_supported()) {
                uint32_t bisterr = _perifs[i].core->run_bist();
                if (bisterr != 0) {
                    throw uhd::runtime_error(str(boost::format("BIST failed! (code: %d)\n") % bisterr));
                } else {
                    double throughput = _perifs[i].core->get_bist_throughput();
                    UHD_LOGGER_INFO("RFNOC DMA FIFO") << (boost::format("BIST passed (Throughput: %.0f MB/s)") % (throughput/1e6)) ;
                }
            } else {
                if (_perifs[i].core->run_bist() == 0) {
                    UHD_LOGGER_INFO("RFNOC DMA FIFO") << "BIST passed";
                } else {
                    throw uhd::runtime_error("BIST failed!");
                }
            }
            _tree->access<int>(get_arg_path("base_addr/value", i))
                .add_coerced_subscriber(boost::bind(&dma_fifo_block_ctrl_impl::resize, this, _1, boost::ref(_perifs[i].depth), i))
                .set(_perifs[i].base_addr)
            ;
            _tree->access<int>(get_arg_path("depth/value", i))
                .add_coerced_subscriber(boost::bind(&dma_fifo_block_ctrl_impl::resize, this, boost::ref(_perifs[i].base_addr), _1, i))
                .set(_perifs[i].depth)
            ;
        }
    }

    void resize(const uint32_t base_addr, const uint32_t depth, const size_t chan) {
        boost::lock_guard<boost::mutex> lock(_config_mutex);
        _perifs[chan].base_addr = base_addr;
        _perifs[chan].depth = depth;
        _perifs[chan].core->resize(base_addr, depth);
    }

    uint32_t get_base_addr(const size_t chan) const {
        return _perifs[chan].base_addr;
    }

    uint32_t get_depth(const size_t chan) const {
        return _perifs[chan].depth;
    }

private:
    struct fifo_perifs_t
    {
        wb_iface::sptr           ctrl;
        dma_fifo_core_3000::sptr core;
        uint32_t                 base_addr;
        uint32_t                 depth;
    };
    std::vector<fifo_perifs_t> _perifs;

    boost::mutex _config_mutex;
};

UHD_RFNOC_BLOCK_REGISTER(dma_fifo_block_ctrl, "DmaFIFO");
