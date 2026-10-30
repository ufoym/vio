// Included inside the compressor and decompressor state classes.
  void per_scan_setup() {
    int data_unit = lossless ? 1 : DCTSIZE;
    if (comps_in_scan == 1) {
      ComponentInfo* c = cur_comp_info[0];
      MCUs_per_row = c->width_in_blocks;
      MCU_rows_in_scan = c->height_in_blocks;
      c->MCU_width = 1;
      c->MCU_height = 1;
      c->MCU_blocks = 1;
#if VIO_SCAN_DECODER
      c->MCU_sample_width = c->DCT_scaled_size;
#else
      c->MCU_sample_width = data_unit;
#endif
      c->last_col_width = 1;
      int tmp = static_cast<int>(c->height_in_blocks % c->v_samp_factor);
      if (tmp == 0) tmp = c->v_samp_factor;
      c->last_row_height = tmp;
      blocks_in_MCU = 1;
      MCU_membership[0] = 0;
    } else {
      if (comps_in_scan <= 0 || comps_in_scan > MAX_COMPS_IN_SCAN)
        VIO_FAIL("Too many color components: %d, max %d", comps_in_scan, MAX_COMPS_IN_SCAN);
      MCUs_per_row = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_width), max_h_samp_factor * data_unit));
      MCU_rows_in_scan = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_height), max_v_samp_factor * data_unit));
      blocks_in_MCU = 0;
      for (int ci = 0; ci < comps_in_scan; ci++) {
        ComponentInfo* c = cur_comp_info[ci];
        c->MCU_width = c->h_samp_factor;
        c->MCU_height = c->v_samp_factor;
        c->MCU_blocks = c->MCU_width * c->MCU_height;
#if VIO_SCAN_DECODER
        c->MCU_sample_width = c->MCU_width * c->DCT_scaled_size;
#else
        c->MCU_sample_width = c->MCU_width * data_unit;
#endif
        int tmp = static_cast<int>(c->width_in_blocks % c->MCU_width);
        if (tmp == 0) tmp = c->MCU_width;
        c->last_col_width = tmp;
        tmp = static_cast<int>(c->height_in_blocks % c->MCU_height);
        if (tmp == 0) tmp = c->MCU_height;
        c->last_row_height = tmp;
        int mcublks = c->MCU_blocks;
#if VIO_SCAN_DECODER
        if (blocks_in_MCU + mcublks > D_MAX_BLOCKS_IN_MCU)
#else
        if (blocks_in_MCU + mcublks > C_MAX_BLOCKS_IN_MCU)
#endif
          VIO_FAIL("Sampling factors too large for interleaved scan");
        while (mcublks-- > 0) MCU_membership[blocks_in_MCU++] = ci;
      }
    }
#if !VIO_SCAN_DECODER
    if (restart_in_rows > 0) {
      long nominal = static_cast<long>(restart_in_rows) * static_cast<long>(MCUs_per_row);
      restart_interval = static_cast<unsigned>(std::min(nominal, 65535L));
    }
#endif
  }
