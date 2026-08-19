// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

module tb_cv32e40p_cluster_core #(
    parameter int unsigned NUM_CORES = 4,
    parameter int unsigned CORE_ID = 0,
    parameter logic [31:0] BOOT_ADDR = 32'h00000080,
    parameter logic [31:0] DM_HALTADDRESS = 32'h1A110800,
    parameter int unsigned SPM_ADDR_WIDTH = 18,
    parameter logic [31:0] SPM_BASE_ADDR = 32'h1800_0000,
    parameter int unsigned DMA_QUEUE_SLOTS = 3
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic fetch_enable_i,

    input  logic [31:0] irq_i,
    input  logic        debug_req_i,
    output logic        irq_ack_o,
    output logic [4:0]  irq_id_o,

    output tb_mem_types_pkg::tb_mem_req_t instr_req_o,
    input  tb_mem_types_pkg::tb_mem_rsp_t instr_rsp_i,

    output tb_mem_types_pkg::tb_mem_req_t shared_req_o,
    input  tb_mem_types_pkg::tb_mem_rsp_t shared_rsp_i,

    input  tb_mem_types_pkg::tb_mem_req_t remote_in_req_i,
    output tb_mem_types_pkg::tb_mem_rsp_t remote_in_rsp_o
);

    localparam int unsigned CORE_IDX_W = (NUM_CORES > 1) ? $clog2(NUM_CORES) : 1;
    localparam logic [31:0] SPM_END_ADDR = SPM_BASE_ADDR + (NUM_CORES * (1 << SPM_ADDR_WIDTH));
    localparam logic [31:0] MMADDR_DMA_BASE = 32'h1500_3000;
    localparam logic [31:0] MMADDR_DMA_SRC  = MMADDR_DMA_BASE + 32'h0000;
    localparam logic [31:0] MMADDR_DMA_DST  = MMADDR_DMA_BASE + 32'h0004;
    localparam logic [31:0] MMADDR_DMA_LEN  = MMADDR_DMA_BASE + 32'h0008;
    localparam logic [31:0] MMADDR_DMA_WAIT = MMADDR_DMA_BASE + 32'h000C;

    logic                  core_data_req;
    logic                  core_data_gnt;
    logic                  core_data_rvalid;
    logic                  core_data_we;
    logic [3:0]            core_data_be;
    logic [31:0]           core_data_addr;
    logic [31:0]           core_data_wdata;
    logic [31:0]           core_data_rdata;

    logic                  core_is_spm;
    logic [CORE_IDX_W-1:0] core_spm_target;
    logic                  core_is_local_spm;
    logic                  core_is_remote_spm;
    logic                  core_is_shared;
    logic                  core_is_dma;
    logic                  core_is_shared_mem;

    logic                  local_spm_en;
    logic                  local_spm_rvalid_q;

    logic                  remote_rsp_valid_q;

    logic                  spm_en;
    logic                  spm_we;
    logic [3:0]            spm_be;
    logic [SPM_ADDR_WIDTH-1:0] spm_addr;
    logic [31:0]           spm_wdata;
    logic [31:0]           spm_rdata;

    tb_mem_types_pkg::tb_mem_req_t cpu_shared_req;
    tb_mem_types_pkg::tb_mem_req_t dma_shared_req;
    tb_mem_types_pkg::tb_mem_req_t local_shared_req [2];
    tb_mem_types_pkg::tb_mem_rsp_t local_shared_rsp [2];
    tb_mem_types_pkg::tb_mem_rsp_t cpu_shared_rsp;
    tb_mem_types_pkg::tb_mem_rsp_t dma_shared_rsp;

    logic dma_cfg_src_valid;
    logic dma_cfg_dst_valid;
    logic dma_cfg_len_valid;
    logic dma_queue_full;
    logic dma_active;
    logic dma_busy;
    logic dma_mmio_rvalid_q;

    cv32e40p_top #(
        .COREV_PULP(0),
        .COREV_CLUSTER(0),
        .FPU(0),
        .ZFINX(0),
        .NUM_MHPMCOUNTERS(1)
    ) cv32e40p_top_i (
        .clk_i              (clk_i),
        .rst_ni             (rst_ni),
        .pulp_clock_en_i    (1'b1),
        .scan_cg_en_i       (1'b0),
        .boot_addr_i        (BOOT_ADDR),
        .mtvec_addr_i       (32'h00000000),
        .dm_halt_addr_i     (DM_HALTADDRESS),
        .hart_id_i          (CORE_ID),
        .dm_exception_addr_i(32'h1A111000),

        .instr_req_o        (instr_req_o.req),
        .instr_gnt_i        (instr_rsp_i.gnt),
        .instr_rvalid_i     (instr_rsp_i.rvalid),
        .instr_addr_o       (instr_req_o.addr),
        .instr_rdata_i      (instr_rsp_i.rdata),

        .data_req_o         (core_data_req),
        .data_gnt_i         (core_data_gnt),
        .data_rvalid_i      (core_data_rvalid),
        .data_we_o          (core_data_we),
        .data_be_o          (core_data_be),
        .data_addr_o        (core_data_addr),
        .data_wdata_o       (core_data_wdata),
        .data_rdata_i       (core_data_rdata),

        .irq_i              (irq_i),
        .irq_ack_o          (irq_ack_o),
        .irq_id_o           (irq_id_o),
        .debug_req_i        (debug_req_i),
        .debug_havereset_o  (),
        .debug_running_o    (),
        .debug_halted_o     (),
        .fetch_enable_i     (fetch_enable_i),
        .core_sleep_o       ()
    );

    assign instr_req_o.we    = 1'b0;
    assign instr_req_o.be    = '0;
    assign instr_req_o.wdata = '0;

    assign core_is_spm =
        (core_data_addr >= SPM_BASE_ADDR) &&
        (core_data_addr <  SPM_END_ADDR);
    assign core_spm_target = core_data_addr[SPM_ADDR_WIDTH +: CORE_IDX_W];
    assign core_is_local_spm = core_is_spm && (core_spm_target == CORE_IDX_W'(CORE_ID));
    assign core_is_remote_spm = core_is_spm && (core_spm_target != CORE_IDX_W'(CORE_ID));
    assign core_is_shared = ~core_is_spm;
    assign core_is_dma = core_is_shared &&
                         ((core_data_addr == MMADDR_DMA_SRC) ||
                          (core_data_addr == MMADDR_DMA_DST) ||
                          (core_data_addr == MMADDR_DMA_LEN) ||
                          (core_data_addr == MMADDR_DMA_WAIT));
    assign core_is_shared_mem = (core_is_remote_spm || core_is_shared) && !core_is_dma;

    assign local_spm_en = core_data_req && core_is_local_spm;

    assign dma_busy = (dma_active === 1'b1);

    assign cpu_shared_req.req   = core_data_req && core_is_shared_mem;
    assign cpu_shared_req.addr  = core_data_addr;
    assign cpu_shared_req.we    = core_data_we;
    assign cpu_shared_req.be    = core_data_be;
    assign cpu_shared_req.wdata = core_data_wdata;

    assign local_shared_req[0] = cpu_shared_req;
    assign local_shared_req[1] = dma_shared_req;
    assign cpu_shared_rsp      = local_shared_rsp[0];
    assign dma_shared_rsp      = local_shared_rsp[1];

    rr_arbiter #(
        .NUM_REQ(2)
    ) local_shared_arb_i (
        .clk_i      (clk_i),
        .rst_ni     (rst_ni),
        .req_mask_i (2'b11),
        .req_i      (local_shared_req),
        .rsp_o      (local_shared_rsp),
        .req_o      (shared_req_o),
        .rsp_i      (shared_rsp_i),
        .req_idx_o  (),
        .req_valid_o()
    );

    assign dma_cfg_src_valid = core_data_req && core_data_gnt && core_data_we && (core_data_addr == MMADDR_DMA_SRC);
    assign dma_cfg_dst_valid = core_data_req && core_data_gnt && core_data_we && (core_data_addr == MMADDR_DMA_DST);
    assign dma_cfg_len_valid = core_data_req && core_data_gnt && core_data_we && (core_data_addr == MMADDR_DMA_LEN);

    tb_core_dma_engine #(
        .DMA_QUEUE_SLOTS(DMA_QUEUE_SLOTS),
        .CORE_ID        (CORE_ID)
    ) dma_i (
        .clk_i         (clk_i),
        .rst_ni        (rst_ni),
        .cfg_src_valid_i(dma_cfg_src_valid),
        .cfg_src_i     (core_data_wdata),
        .cfg_dst_valid_i(dma_cfg_dst_valid),
        .cfg_dst_i     (core_data_wdata),
        .cfg_len_valid_i(dma_cfg_len_valid),
        .cfg_len_i     (core_data_wdata),
        .queue_full_o  (dma_queue_full),
        .active_o      (dma_active),
        .req_o         (dma_shared_req),
        .rsp_i         (dma_shared_rsp)
    );

    always_comb begin
        if (local_spm_en) begin
            spm_en    = 1'b1;
            spm_we    = core_data_we;
            spm_be    = core_data_be;
            spm_addr  = core_data_addr[SPM_ADDR_WIDTH-1:0];
            spm_wdata = core_data_wdata;
        end else if (remote_in_req_i.req) begin
            spm_en    = 1'b1;
            spm_we    = remote_in_req_i.we;
            spm_be    = remote_in_req_i.be;
            spm_addr  = remote_in_req_i.addr[SPM_ADDR_WIDTH-1:0];
            spm_wdata = remote_in_req_i.wdata;
        end else begin
            spm_en    = 1'b0;
            spm_we    = 1'b0;
            spm_be    = '0;
            spm_addr  = '0;
            spm_wdata = '0;
        end
    end

    assign remote_in_rsp_o.gnt = remote_in_req_i.req && !local_spm_en;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            local_spm_rvalid_q <= 1'b0;
            remote_rsp_valid_q <= 1'b0;
        end else begin
            local_spm_rvalid_q <= local_spm_en;
            remote_rsp_valid_q <= remote_in_rsp_o.gnt;
        end
    end

    assign remote_in_rsp_o.rvalid = remote_rsp_valid_q;
    assign remote_in_rsp_o.rdata  = spm_rdata;

    always_comb begin
        core_data_gnt = 1'b0;
        if (local_spm_en) begin
            core_data_gnt = 1'b1;
        end else if (core_data_req && core_is_dma) begin
            if (core_data_we) begin
                if (core_data_addr == MMADDR_DMA_LEN)
                    core_data_gnt = (dma_queue_full === 1'b1) ? 1'b0 : 1'b1;
                else
                    core_data_gnt = 1'b1;
            end
            else if (core_data_addr == MMADDR_DMA_WAIT)
                core_data_gnt = !dma_busy;
            else
                core_data_gnt = 1'b1;
        end else if (core_data_req && core_is_shared_mem) begin
            core_data_gnt = cpu_shared_rsp.gnt;
        end
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni)
            dma_mmio_rvalid_q <= 1'b0;
        else
            dma_mmio_rvalid_q <= core_data_req && core_data_gnt && core_is_dma;
    end

    always_comb begin
        core_data_rvalid = 1'b0;
        core_data_rdata  = '0;

        if (local_spm_rvalid_q) begin
            core_data_rvalid = 1'b1;
            core_data_rdata  = spm_rdata;
        end
        if (cpu_shared_rsp.rvalid) begin
            core_data_rvalid = 1'b1;
            core_data_rdata  = cpu_shared_rsp.rdata;
        end
        if (dma_mmio_rvalid_q) begin
            core_data_rvalid = 1'b1;
            core_data_rdata  = '0;
        end
    end

    scratchpad_ram #(
        .ADDR_WIDTH(SPM_ADDR_WIDTH)
    ) spm_i (
        .clk_i   (clk_i),
        .en_i    (spm_en),
        .we_i    (spm_we),
        .be_i    (spm_be),
        .addr_i  (spm_addr),
        .wdata_i (spm_wdata),
        .rdata_o (spm_rdata)
    );

endmodule
