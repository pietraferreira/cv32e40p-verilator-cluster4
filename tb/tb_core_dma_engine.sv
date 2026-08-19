// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

module tb_core_dma_engine #(
    parameter int unsigned DMA_QUEUE_SLOTS = 3,
    parameter int unsigned CORE_ID = 0
) (
    input  logic clk_i,
    input  logic rst_ni,

    input  logic cfg_src_valid_i,
    input  logic [31:0] cfg_src_i,
    input  logic cfg_dst_valid_i,
    input  logic [31:0] cfg_dst_i,
    input  logic cfg_len_valid_i,
    input  logic [31:0] cfg_len_i,

    output logic queue_full_o,
    output logic active_o,

    output tb_mem_types_pkg::tb_mem_req_t req_o,
    input  tb_mem_types_pkg::tb_mem_rsp_t rsp_i
);

    localparam int unsigned SLOT_W = (DMA_QUEUE_SLOTS > 1) ? $clog2(DMA_QUEUE_SLOTS) : 1;
    localparam int unsigned COUNT_W = $clog2(DMA_QUEUE_SLOTS + 1);

    typedef enum logic [2:0] {
        DMA_IDLE,
        DMA_RD_REQ,
        DMA_RD_WAIT,
        DMA_WR_REQ,
        DMA_WR_WAIT
    } dma_state_t;

    logic [31:0] cfg_src_q;
    logic [31:0] cfg_dst_q;

    logic [31:0] q_src [DMA_QUEUE_SLOTS];
    logic [31:0] q_dst [DMA_QUEUE_SLOTS];
    logic [31:0] q_len [DMA_QUEUE_SLOTS];
    logic [SLOT_W-1:0] q_head_q;
    logic [SLOT_W-1:0] q_tail_q;
    logic [COUNT_W-1:0] q_count_q;

    dma_state_t state_q;

    logic [31:0] cur_src_q;
    logic [31:0] cur_dst_q;
    logic [31:0] cur_len_q;
    logic [31:0] rd_word_q;

    logic [SLOT_W-1:0] next_head;
    logic [SLOT_W-1:0] next_tail;
    logic [7:0] src_byte;
    logic [1:0] src_lane;
    logic [1:0] dst_lane;
    logic src_is_aligned;
    logic dst_is_aligned;
    logic word_remains;
    logic word_step;
    logic [31:0] step;

    // Free-running cycle count, used only to timestamp +dma_trace output.
    int unsigned dbg_cyc;

    always_comb begin
        next_head = (q_head_q == SLOT_W'(DMA_QUEUE_SLOTS - 1)) ? '0 : (q_head_q + 1'b1);
        next_tail = (q_tail_q == SLOT_W'(DMA_QUEUE_SLOTS - 1)) ? '0 : (q_tail_q + 1'b1);
        src_lane = cur_src_q[1:0];
        dst_lane = cur_dst_q[1:0];
        src_byte = rd_word_q[(src_lane * 8) +: 8];

        // How much this iteration moves. A whole word can go in one read/write
        // pair only when both ends sit on a 4-byte boundary and a full word is
        // still left to copy, anything else falls back to the single byte the 
        // engine has always moved.
        src_is_aligned = (src_lane == 2'b00);
        dst_is_aligned = (dst_lane == 2'b00);
        word_remains   = (cur_len_q >= 32'd4);

        if (src_is_aligned && dst_is_aligned && word_remains) begin
            word_step = 1'b1;
            step      = 32'd4;
        end else begin
            word_step = 1'b0;
            step      = 32'd1;
        end

        req_o = '0;
        case (state_q)
            DMA_RD_REQ: begin
                req_o.req   = 1'b1;
                req_o.addr  = {cur_src_q[31:2], 2'b00};
                req_o.we    = 1'b0;
                req_o.be    = 4'b1111;
                req_o.wdata = '0;
            end
            DMA_WR_REQ: begin
                req_o.req   = 1'b1;
                req_o.addr  = {cur_dst_q[31:2], 2'b00};
                req_o.we    = 1'b1;
                if (word_step) begin
                    // All four byte enables on, so all four bytes land.
                    req_o.be    = 4'b1111;
                    req_o.wdata = rd_word_q;
                end else begin
                    // One byte, shifted into its lane within the word, with
                    // only that lane's byte enable on so the neighbours are
                    // untouched.
                    req_o.be    = 4'b0001 << dst_lane;
                    req_o.wdata = {24'h0, src_byte} << (dst_lane * 8);
                end
            end
            default: begin
                req_o = '0;
            end
        endcase

        queue_full_o = (q_count_q == DMA_QUEUE_SLOTS);
        active_o = (state_q != DMA_IDLE) || (q_count_q != '0);
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            cfg_src_q <= '0;
            cfg_dst_q <= '0;
            dbg_cyc <= 0;
            q_head_q <= '0;
            q_tail_q <= '0;
            q_count_q <= '0;
            state_q <= DMA_IDLE;
            cur_src_q <= '0;
            cur_dst_q <= '0;
            cur_len_q <= '0;
            rd_word_q <= '0;
        end else begin
            dbg_cyc <= dbg_cyc + 1;

            if (cfg_src_valid_i)
                cfg_src_q <= cfg_src_i;
            if (cfg_dst_valid_i)
                cfg_dst_q <= cfg_dst_i;

            if (cfg_len_valid_i && (q_count_q < DMA_QUEUE_SLOTS)) begin
                q_src[q_tail_q] <= cfg_src_q;
                q_dst[q_tail_q] <= cfg_dst_q;
                q_len[q_tail_q] <= cfg_len_i;
                q_tail_q <= next_tail;
                q_count_q <= q_count_q + 1'b1;
                if ($test$plusargs("dma_debug"))
                    $display("[DMA] enqueue src=%08x dst=%08x len=%0d", cfg_src_q, cfg_dst_q, cfg_len_i);
            end

            case (state_q)
                DMA_IDLE: begin
                    if (q_count_q != '0) begin
                        cur_src_q <= q_src[q_head_q];
                        cur_dst_q <= q_dst[q_head_q];
                        cur_len_q <= q_len[q_head_q];
                        q_head_q <= next_head;
                        q_count_q <= q_count_q - 1'b1;
                        if (q_len[q_head_q] != 0) begin
                            state_q <= DMA_RD_REQ;
                            if ($test$plusargs("dma_debug"))
                                $display("[DMA] start src=%08x dst=%08x len=%0d", q_src[q_head_q], q_dst[q_head_q], q_len[q_head_q]);
                        end
                    end
                end

                DMA_RD_REQ: begin
                    if (rsp_i.gnt) begin
                        if ($test$plusargs("dma_trace"))
                            $display("[DMA%0d] %8d RD  addr=%08x be=1111", CORE_ID,
                                     dbg_cyc, {cur_src_q[31:2], 2'b00});
                        state_q <= DMA_RD_WAIT;
                    end
                end

                DMA_RD_WAIT: begin
                    if (rsp_i.rvalid) begin
                        if ($test$plusargs("dma_trace"))
                            $display("[DMA%0d] %8d     rdata=%08x  taking %0d of 4 bytes",
                                     CORE_ID, dbg_cyc, rsp_i.rdata, step);
                        rd_word_q <= rsp_i.rdata;
                        state_q <= DMA_WR_REQ;
                    end
                end

                DMA_WR_REQ: begin
                    if (rsp_i.gnt) begin
                        if ($test$plusargs("dma_trace"))
                            $display("[DMA%0d] %8d WR  addr=%08x be=%04b wdata=%08x",
                                     CORE_ID, dbg_cyc, {cur_dst_q[31:2], 2'b00},
                                     req_o.be, req_o.wdata);
                        state_q <= DMA_WR_WAIT;
                    end
                end

                DMA_WR_WAIT: begin
                    if (rsp_i.rvalid) begin
                        cur_src_q <= cur_src_q + step;
                        cur_dst_q <= cur_dst_q + step;
                        cur_len_q <= cur_len_q - step;
                        // Done when this iteration consumed everything that was
                        // left.
                        if (cur_len_q <= step) begin
                            state_q <= DMA_IDLE;
                            if ($test$plusargs("dma_debug") || $test$plusargs("dma_trace"))
                                $display("[DMA%0d] %8d done", CORE_ID, dbg_cyc);
                        end else
                            state_q <= DMA_RD_REQ;
                    end
                end
                default: state_q <= DMA_IDLE;
            endcase
        end
    end

endmodule
