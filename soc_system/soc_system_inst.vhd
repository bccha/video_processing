	component soc_system is
		port (
			button_pio_external_connection_export : in    std_logic_vector(1 downto 0)  := (others => 'X'); -- export
			clk_clk                               : in    std_logic                     := 'X';             -- clk
			dipsw_pio_external_connection_export  : in    std_logic_vector(3 downto 0)  := (others => 'X'); -- export
			hps_0_f2h_cold_reset_req_reset_n      : in    std_logic                     := 'X';             -- reset_n
			hps_0_f2h_debug_reset_req_reset_n     : in    std_logic                     := 'X';             -- reset_n
			hps_0_f2h_stm_hw_events_stm_hwevents  : in    std_logic_vector(27 downto 0) := (others => 'X'); -- stm_hwevents
			hps_0_f2h_warm_reset_req_reset_n      : in    std_logic                     := 'X';             -- reset_n
			hps_0_h2f_reset_reset_n               : out   std_logic;                                        -- reset_n
			hps_0_hps_io_hps_io_emac1_inst_TX_CLK : out   std_logic;                                        -- hps_io_emac1_inst_TX_CLK
			hps_0_hps_io_hps_io_emac1_inst_TXD0   : out   std_logic;                                        -- hps_io_emac1_inst_TXD0
			hps_0_hps_io_hps_io_emac1_inst_TXD1   : out   std_logic;                                        -- hps_io_emac1_inst_TXD1
			hps_0_hps_io_hps_io_emac1_inst_TXD2   : out   std_logic;                                        -- hps_io_emac1_inst_TXD2
			hps_0_hps_io_hps_io_emac1_inst_TXD3   : out   std_logic;                                        -- hps_io_emac1_inst_TXD3
			hps_0_hps_io_hps_io_emac1_inst_RXD0   : in    std_logic                     := 'X';             -- hps_io_emac1_inst_RXD0
			hps_0_hps_io_hps_io_emac1_inst_MDIO   : inout std_logic                     := 'X';             -- hps_io_emac1_inst_MDIO
			hps_0_hps_io_hps_io_emac1_inst_MDC    : out   std_logic;                                        -- hps_io_emac1_inst_MDC
			hps_0_hps_io_hps_io_emac1_inst_RX_CTL : in    std_logic                     := 'X';             -- hps_io_emac1_inst_RX_CTL
			hps_0_hps_io_hps_io_emac1_inst_TX_CTL : out   std_logic;                                        -- hps_io_emac1_inst_TX_CTL
			hps_0_hps_io_hps_io_emac1_inst_RX_CLK : in    std_logic                     := 'X';             -- hps_io_emac1_inst_RX_CLK
			hps_0_hps_io_hps_io_emac1_inst_RXD1   : in    std_logic                     := 'X';             -- hps_io_emac1_inst_RXD1
			hps_0_hps_io_hps_io_emac1_inst_RXD2   : in    std_logic                     := 'X';             -- hps_io_emac1_inst_RXD2
			hps_0_hps_io_hps_io_emac1_inst_RXD3   : in    std_logic                     := 'X';             -- hps_io_emac1_inst_RXD3
			hps_0_hps_io_hps_io_sdio_inst_CMD     : inout std_logic                     := 'X';             -- hps_io_sdio_inst_CMD
			hps_0_hps_io_hps_io_sdio_inst_D0      : inout std_logic                     := 'X';             -- hps_io_sdio_inst_D0
			hps_0_hps_io_hps_io_sdio_inst_D1      : inout std_logic                     := 'X';             -- hps_io_sdio_inst_D1
			hps_0_hps_io_hps_io_sdio_inst_CLK     : out   std_logic;                                        -- hps_io_sdio_inst_CLK
			hps_0_hps_io_hps_io_sdio_inst_D2      : inout std_logic                     := 'X';             -- hps_io_sdio_inst_D2
			hps_0_hps_io_hps_io_sdio_inst_D3      : inout std_logic                     := 'X';             -- hps_io_sdio_inst_D3
			hps_0_hps_io_hps_io_usb1_inst_D0      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D0
			hps_0_hps_io_hps_io_usb1_inst_D1      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D1
			hps_0_hps_io_hps_io_usb1_inst_D2      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D2
			hps_0_hps_io_hps_io_usb1_inst_D3      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D3
			hps_0_hps_io_hps_io_usb1_inst_D4      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D4
			hps_0_hps_io_hps_io_usb1_inst_D5      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D5
			hps_0_hps_io_hps_io_usb1_inst_D6      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D6
			hps_0_hps_io_hps_io_usb1_inst_D7      : inout std_logic                     := 'X';             -- hps_io_usb1_inst_D7
			hps_0_hps_io_hps_io_usb1_inst_CLK     : in    std_logic                     := 'X';             -- hps_io_usb1_inst_CLK
			hps_0_hps_io_hps_io_usb1_inst_STP     : out   std_logic;                                        -- hps_io_usb1_inst_STP
			hps_0_hps_io_hps_io_usb1_inst_DIR     : in    std_logic                     := 'X';             -- hps_io_usb1_inst_DIR
			hps_0_hps_io_hps_io_usb1_inst_NXT     : in    std_logic                     := 'X';             -- hps_io_usb1_inst_NXT
			hps_0_hps_io_hps_io_spim1_inst_CLK    : out   std_logic;                                        -- hps_io_spim1_inst_CLK
			hps_0_hps_io_hps_io_spim1_inst_MOSI   : out   std_logic;                                        -- hps_io_spim1_inst_MOSI
			hps_0_hps_io_hps_io_spim1_inst_MISO   : in    std_logic                     := 'X';             -- hps_io_spim1_inst_MISO
			hps_0_hps_io_hps_io_spim1_inst_SS0    : out   std_logic;                                        -- hps_io_spim1_inst_SS0
			hps_0_hps_io_hps_io_uart0_inst_RX     : in    std_logic                     := 'X';             -- hps_io_uart0_inst_RX
			hps_0_hps_io_hps_io_uart0_inst_TX     : out   std_logic;                                        -- hps_io_uart0_inst_TX
			hps_0_hps_io_hps_io_i2c0_inst_SDA     : inout std_logic                     := 'X';             -- hps_io_i2c0_inst_SDA
			hps_0_hps_io_hps_io_i2c0_inst_SCL     : inout std_logic                     := 'X';             -- hps_io_i2c0_inst_SCL
			hps_0_hps_io_hps_io_i2c1_inst_SDA     : inout std_logic                     := 'X';             -- hps_io_i2c1_inst_SDA
			hps_0_hps_io_hps_io_i2c1_inst_SCL     : inout std_logic                     := 'X';             -- hps_io_i2c1_inst_SCL
			hps_0_hps_io_hps_io_gpio_inst_GPIO09  : inout std_logic                     := 'X';             -- hps_io_gpio_inst_GPIO09
			hps_0_hps_io_hps_io_gpio_inst_GPIO35  : inout std_logic                     := 'X';             -- hps_io_gpio_inst_GPIO35
			hps_0_hps_io_hps_io_gpio_inst_GPIO40  : inout std_logic                     := 'X';             -- hps_io_gpio_inst_GPIO40
			hps_0_hps_io_hps_io_gpio_inst_GPIO53  : inout std_logic                     := 'X';             -- hps_io_gpio_inst_GPIO53
			hps_0_hps_io_hps_io_gpio_inst_GPIO54  : inout std_logic                     := 'X';             -- hps_io_gpio_inst_GPIO54
			hps_0_hps_io_hps_io_gpio_inst_GPIO61  : inout std_logic                     := 'X';             -- hps_io_gpio_inst_GPIO61
			i2c_hdmi_sda_in                       : in    std_logic                     := 'X';             -- sda_in
			i2c_hdmi_scl_in                       : in    std_logic                     := 'X';             -- scl_in
			i2c_hdmi_sda_oe                       : out   std_logic;                                        -- sda_oe
			i2c_hdmi_scl_oe                       : out   std_logic;                                        -- scl_oe
			led_pio_external_connection_export    : out   std_logic_vector(6 downto 0);                     -- export
			memory_mem_a                          : out   std_logic_vector(14 downto 0);                    -- mem_a
			memory_mem_ba                         : out   std_logic_vector(2 downto 0);                     -- mem_ba
			memory_mem_ck                         : out   std_logic;                                        -- mem_ck
			memory_mem_ck_n                       : out   std_logic;                                        -- mem_ck_n
			memory_mem_cke                        : out   std_logic;                                        -- mem_cke
			memory_mem_cs_n                       : out   std_logic;                                        -- mem_cs_n
			memory_mem_ras_n                      : out   std_logic;                                        -- mem_ras_n
			memory_mem_cas_n                      : out   std_logic;                                        -- mem_cas_n
			memory_mem_we_n                       : out   std_logic;                                        -- mem_we_n
			memory_mem_reset_n                    : out   std_logic;                                        -- mem_reset_n
			memory_mem_dq                         : inout std_logic_vector(31 downto 0) := (others => 'X'); -- mem_dq
			memory_mem_dqs                        : inout std_logic_vector(3 downto 0)  := (others => 'X'); -- mem_dqs
			memory_mem_dqs_n                      : inout std_logic_vector(3 downto 0)  := (others => 'X'); -- mem_dqs_n
			memory_mem_odt                        : out   std_logic;                                        -- mem_odt
			memory_mem_dm                         : out   std_logic_vector(3 downto 0);                     -- mem_dm
			memory_oct_rzqin                      : in    std_logic                     := 'X';             -- oct_rzqin
			pll_outclk_clk                        : out   std_logic;                                        -- clk
			reset_reset_n                         : in    std_logic                     := 'X';             -- reset_n
			video_dma_s_waitrequest               : out   std_logic;                                        -- waitrequest
			video_dma_s_readdata                  : out   std_logic_vector(31 downto 0);                    -- readdata
			video_dma_s_readdatavalid             : out   std_logic;                                        -- readdatavalid
			video_dma_s_burstcount                : in    std_logic_vector(8 downto 0)  := (others => 'X'); -- burstcount
			video_dma_s_writedata                 : in    std_logic_vector(31 downto 0) := (others => 'X'); -- writedata
			video_dma_s_address                   : in    std_logic_vector(31 downto 0) := (others => 'X'); -- address
			video_dma_s_write                     : in    std_logic                     := 'X';             -- write
			video_dma_s_read                      : in    std_logic                     := 'X';             -- read
			video_dma_s_byteenable                : in    std_logic_vector(3 downto 0)  := (others => 'X'); -- byteenable
			video_dma_s_debugaccess               : in    std_logic                     := 'X';             -- debugaccess
			hdmi_sync_master_waitrequest          : in    std_logic                     := 'X';             -- waitrequest
			hdmi_sync_master_readdata             : in    std_logic_vector(31 downto 0) := (others => 'X'); -- readdata
			hdmi_sync_master_readdatavalid        : in    std_logic                     := 'X';             -- readdatavalid
			hdmi_sync_master_burstcount           : out   std_logic_vector(0 downto 0);                     -- burstcount
			hdmi_sync_master_writedata            : out   std_logic_vector(31 downto 0);                    -- writedata
			hdmi_sync_master_address              : out   std_logic_vector(2 downto 0);                     -- address
			hdmi_sync_master_write                : out   std_logic;                                        -- write
			hdmi_sync_master_read                 : out   std_logic;                                        -- read
			hdmi_sync_master_byteenable           : out   std_logic_vector(3 downto 0);                     -- byteenable
			hdmi_sync_master_debugaccess          : out   std_logic                                         -- debugaccess
		);
	end component soc_system;

	u0 : component soc_system
		port map (
			button_pio_external_connection_export => CONNECTED_TO_button_pio_external_connection_export, -- button_pio_external_connection.export
			clk_clk                               => CONNECTED_TO_clk_clk,                               --                            clk.clk
			dipsw_pio_external_connection_export  => CONNECTED_TO_dipsw_pio_external_connection_export,  --  dipsw_pio_external_connection.export
			hps_0_f2h_cold_reset_req_reset_n      => CONNECTED_TO_hps_0_f2h_cold_reset_req_reset_n,      --       hps_0_f2h_cold_reset_req.reset_n
			hps_0_f2h_debug_reset_req_reset_n     => CONNECTED_TO_hps_0_f2h_debug_reset_req_reset_n,     --      hps_0_f2h_debug_reset_req.reset_n
			hps_0_f2h_stm_hw_events_stm_hwevents  => CONNECTED_TO_hps_0_f2h_stm_hw_events_stm_hwevents,  --        hps_0_f2h_stm_hw_events.stm_hwevents
			hps_0_f2h_warm_reset_req_reset_n      => CONNECTED_TO_hps_0_f2h_warm_reset_req_reset_n,      --       hps_0_f2h_warm_reset_req.reset_n
			hps_0_h2f_reset_reset_n               => CONNECTED_TO_hps_0_h2f_reset_reset_n,               --                hps_0_h2f_reset.reset_n
			hps_0_hps_io_hps_io_emac1_inst_TX_CLK => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_TX_CLK, --                   hps_0_hps_io.hps_io_emac1_inst_TX_CLK
			hps_0_hps_io_hps_io_emac1_inst_TXD0   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_TXD0,   --                               .hps_io_emac1_inst_TXD0
			hps_0_hps_io_hps_io_emac1_inst_TXD1   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_TXD1,   --                               .hps_io_emac1_inst_TXD1
			hps_0_hps_io_hps_io_emac1_inst_TXD2   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_TXD2,   --                               .hps_io_emac1_inst_TXD2
			hps_0_hps_io_hps_io_emac1_inst_TXD3   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_TXD3,   --                               .hps_io_emac1_inst_TXD3
			hps_0_hps_io_hps_io_emac1_inst_RXD0   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_RXD0,   --                               .hps_io_emac1_inst_RXD0
			hps_0_hps_io_hps_io_emac1_inst_MDIO   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_MDIO,   --                               .hps_io_emac1_inst_MDIO
			hps_0_hps_io_hps_io_emac1_inst_MDC    => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_MDC,    --                               .hps_io_emac1_inst_MDC
			hps_0_hps_io_hps_io_emac1_inst_RX_CTL => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_RX_CTL, --                               .hps_io_emac1_inst_RX_CTL
			hps_0_hps_io_hps_io_emac1_inst_TX_CTL => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_TX_CTL, --                               .hps_io_emac1_inst_TX_CTL
			hps_0_hps_io_hps_io_emac1_inst_RX_CLK => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_RX_CLK, --                               .hps_io_emac1_inst_RX_CLK
			hps_0_hps_io_hps_io_emac1_inst_RXD1   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_RXD1,   --                               .hps_io_emac1_inst_RXD1
			hps_0_hps_io_hps_io_emac1_inst_RXD2   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_RXD2,   --                               .hps_io_emac1_inst_RXD2
			hps_0_hps_io_hps_io_emac1_inst_RXD3   => CONNECTED_TO_hps_0_hps_io_hps_io_emac1_inst_RXD3,   --                               .hps_io_emac1_inst_RXD3
			hps_0_hps_io_hps_io_sdio_inst_CMD     => CONNECTED_TO_hps_0_hps_io_hps_io_sdio_inst_CMD,     --                               .hps_io_sdio_inst_CMD
			hps_0_hps_io_hps_io_sdio_inst_D0      => CONNECTED_TO_hps_0_hps_io_hps_io_sdio_inst_D0,      --                               .hps_io_sdio_inst_D0
			hps_0_hps_io_hps_io_sdio_inst_D1      => CONNECTED_TO_hps_0_hps_io_hps_io_sdio_inst_D1,      --                               .hps_io_sdio_inst_D1
			hps_0_hps_io_hps_io_sdio_inst_CLK     => CONNECTED_TO_hps_0_hps_io_hps_io_sdio_inst_CLK,     --                               .hps_io_sdio_inst_CLK
			hps_0_hps_io_hps_io_sdio_inst_D2      => CONNECTED_TO_hps_0_hps_io_hps_io_sdio_inst_D2,      --                               .hps_io_sdio_inst_D2
			hps_0_hps_io_hps_io_sdio_inst_D3      => CONNECTED_TO_hps_0_hps_io_hps_io_sdio_inst_D3,      --                               .hps_io_sdio_inst_D3
			hps_0_hps_io_hps_io_usb1_inst_D0      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D0,      --                               .hps_io_usb1_inst_D0
			hps_0_hps_io_hps_io_usb1_inst_D1      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D1,      --                               .hps_io_usb1_inst_D1
			hps_0_hps_io_hps_io_usb1_inst_D2      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D2,      --                               .hps_io_usb1_inst_D2
			hps_0_hps_io_hps_io_usb1_inst_D3      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D3,      --                               .hps_io_usb1_inst_D3
			hps_0_hps_io_hps_io_usb1_inst_D4      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D4,      --                               .hps_io_usb1_inst_D4
			hps_0_hps_io_hps_io_usb1_inst_D5      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D5,      --                               .hps_io_usb1_inst_D5
			hps_0_hps_io_hps_io_usb1_inst_D6      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D6,      --                               .hps_io_usb1_inst_D6
			hps_0_hps_io_hps_io_usb1_inst_D7      => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_D7,      --                               .hps_io_usb1_inst_D7
			hps_0_hps_io_hps_io_usb1_inst_CLK     => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_CLK,     --                               .hps_io_usb1_inst_CLK
			hps_0_hps_io_hps_io_usb1_inst_STP     => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_STP,     --                               .hps_io_usb1_inst_STP
			hps_0_hps_io_hps_io_usb1_inst_DIR     => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_DIR,     --                               .hps_io_usb1_inst_DIR
			hps_0_hps_io_hps_io_usb1_inst_NXT     => CONNECTED_TO_hps_0_hps_io_hps_io_usb1_inst_NXT,     --                               .hps_io_usb1_inst_NXT
			hps_0_hps_io_hps_io_spim1_inst_CLK    => CONNECTED_TO_hps_0_hps_io_hps_io_spim1_inst_CLK,    --                               .hps_io_spim1_inst_CLK
			hps_0_hps_io_hps_io_spim1_inst_MOSI   => CONNECTED_TO_hps_0_hps_io_hps_io_spim1_inst_MOSI,   --                               .hps_io_spim1_inst_MOSI
			hps_0_hps_io_hps_io_spim1_inst_MISO   => CONNECTED_TO_hps_0_hps_io_hps_io_spim1_inst_MISO,   --                               .hps_io_spim1_inst_MISO
			hps_0_hps_io_hps_io_spim1_inst_SS0    => CONNECTED_TO_hps_0_hps_io_hps_io_spim1_inst_SS0,    --                               .hps_io_spim1_inst_SS0
			hps_0_hps_io_hps_io_uart0_inst_RX     => CONNECTED_TO_hps_0_hps_io_hps_io_uart0_inst_RX,     --                               .hps_io_uart0_inst_RX
			hps_0_hps_io_hps_io_uart0_inst_TX     => CONNECTED_TO_hps_0_hps_io_hps_io_uart0_inst_TX,     --                               .hps_io_uart0_inst_TX
			hps_0_hps_io_hps_io_i2c0_inst_SDA     => CONNECTED_TO_hps_0_hps_io_hps_io_i2c0_inst_SDA,     --                               .hps_io_i2c0_inst_SDA
			hps_0_hps_io_hps_io_i2c0_inst_SCL     => CONNECTED_TO_hps_0_hps_io_hps_io_i2c0_inst_SCL,     --                               .hps_io_i2c0_inst_SCL
			hps_0_hps_io_hps_io_i2c1_inst_SDA     => CONNECTED_TO_hps_0_hps_io_hps_io_i2c1_inst_SDA,     --                               .hps_io_i2c1_inst_SDA
			hps_0_hps_io_hps_io_i2c1_inst_SCL     => CONNECTED_TO_hps_0_hps_io_hps_io_i2c1_inst_SCL,     --                               .hps_io_i2c1_inst_SCL
			hps_0_hps_io_hps_io_gpio_inst_GPIO09  => CONNECTED_TO_hps_0_hps_io_hps_io_gpio_inst_GPIO09,  --                               .hps_io_gpio_inst_GPIO09
			hps_0_hps_io_hps_io_gpio_inst_GPIO35  => CONNECTED_TO_hps_0_hps_io_hps_io_gpio_inst_GPIO35,  --                               .hps_io_gpio_inst_GPIO35
			hps_0_hps_io_hps_io_gpio_inst_GPIO40  => CONNECTED_TO_hps_0_hps_io_hps_io_gpio_inst_GPIO40,  --                               .hps_io_gpio_inst_GPIO40
			hps_0_hps_io_hps_io_gpio_inst_GPIO53  => CONNECTED_TO_hps_0_hps_io_hps_io_gpio_inst_GPIO53,  --                               .hps_io_gpio_inst_GPIO53
			hps_0_hps_io_hps_io_gpio_inst_GPIO54  => CONNECTED_TO_hps_0_hps_io_hps_io_gpio_inst_GPIO54,  --                               .hps_io_gpio_inst_GPIO54
			hps_0_hps_io_hps_io_gpio_inst_GPIO61  => CONNECTED_TO_hps_0_hps_io_hps_io_gpio_inst_GPIO61,  --                               .hps_io_gpio_inst_GPIO61
			i2c_hdmi_sda_in                       => CONNECTED_TO_i2c_hdmi_sda_in,                       --                       i2c_hdmi.sda_in
			i2c_hdmi_scl_in                       => CONNECTED_TO_i2c_hdmi_scl_in,                       --                               .scl_in
			i2c_hdmi_sda_oe                       => CONNECTED_TO_i2c_hdmi_sda_oe,                       --                               .sda_oe
			i2c_hdmi_scl_oe                       => CONNECTED_TO_i2c_hdmi_scl_oe,                       --                               .scl_oe
			led_pio_external_connection_export    => CONNECTED_TO_led_pio_external_connection_export,    --    led_pio_external_connection.export
			memory_mem_a                          => CONNECTED_TO_memory_mem_a,                          --                         memory.mem_a
			memory_mem_ba                         => CONNECTED_TO_memory_mem_ba,                         --                               .mem_ba
			memory_mem_ck                         => CONNECTED_TO_memory_mem_ck,                         --                               .mem_ck
			memory_mem_ck_n                       => CONNECTED_TO_memory_mem_ck_n,                       --                               .mem_ck_n
			memory_mem_cke                        => CONNECTED_TO_memory_mem_cke,                        --                               .mem_cke
			memory_mem_cs_n                       => CONNECTED_TO_memory_mem_cs_n,                       --                               .mem_cs_n
			memory_mem_ras_n                      => CONNECTED_TO_memory_mem_ras_n,                      --                               .mem_ras_n
			memory_mem_cas_n                      => CONNECTED_TO_memory_mem_cas_n,                      --                               .mem_cas_n
			memory_mem_we_n                       => CONNECTED_TO_memory_mem_we_n,                       --                               .mem_we_n
			memory_mem_reset_n                    => CONNECTED_TO_memory_mem_reset_n,                    --                               .mem_reset_n
			memory_mem_dq                         => CONNECTED_TO_memory_mem_dq,                         --                               .mem_dq
			memory_mem_dqs                        => CONNECTED_TO_memory_mem_dqs,                        --                               .mem_dqs
			memory_mem_dqs_n                      => CONNECTED_TO_memory_mem_dqs_n,                      --                               .mem_dqs_n
			memory_mem_odt                        => CONNECTED_TO_memory_mem_odt,                        --                               .mem_odt
			memory_mem_dm                         => CONNECTED_TO_memory_mem_dm,                         --                               .mem_dm
			memory_oct_rzqin                      => CONNECTED_TO_memory_oct_rzqin,                      --                               .oct_rzqin
			pll_outclk_clk                        => CONNECTED_TO_pll_outclk_clk,                        --                     pll_outclk.clk
			reset_reset_n                         => CONNECTED_TO_reset_reset_n,                         --                          reset.reset_n
			video_dma_s_waitrequest               => CONNECTED_TO_video_dma_s_waitrequest,               --                    video_dma_s.waitrequest
			video_dma_s_readdata                  => CONNECTED_TO_video_dma_s_readdata,                  --                               .readdata
			video_dma_s_readdatavalid             => CONNECTED_TO_video_dma_s_readdatavalid,             --                               .readdatavalid
			video_dma_s_burstcount                => CONNECTED_TO_video_dma_s_burstcount,                --                               .burstcount
			video_dma_s_writedata                 => CONNECTED_TO_video_dma_s_writedata,                 --                               .writedata
			video_dma_s_address                   => CONNECTED_TO_video_dma_s_address,                   --                               .address
			video_dma_s_write                     => CONNECTED_TO_video_dma_s_write,                     --                               .write
			video_dma_s_read                      => CONNECTED_TO_video_dma_s_read,                      --                               .read
			video_dma_s_byteenable                => CONNECTED_TO_video_dma_s_byteenable,                --                               .byteenable
			video_dma_s_debugaccess               => CONNECTED_TO_video_dma_s_debugaccess,               --                               .debugaccess
			hdmi_sync_master_waitrequest          => CONNECTED_TO_hdmi_sync_master_waitrequest,          --               hdmi_sync_master.waitrequest
			hdmi_sync_master_readdata             => CONNECTED_TO_hdmi_sync_master_readdata,             --                               .readdata
			hdmi_sync_master_readdatavalid        => CONNECTED_TO_hdmi_sync_master_readdatavalid,        --                               .readdatavalid
			hdmi_sync_master_burstcount           => CONNECTED_TO_hdmi_sync_master_burstcount,           --                               .burstcount
			hdmi_sync_master_writedata            => CONNECTED_TO_hdmi_sync_master_writedata,            --                               .writedata
			hdmi_sync_master_address              => CONNECTED_TO_hdmi_sync_master_address,              --                               .address
			hdmi_sync_master_write                => CONNECTED_TO_hdmi_sync_master_write,                --                               .write
			hdmi_sync_master_read                 => CONNECTED_TO_hdmi_sync_master_read,                 --                               .read
			hdmi_sync_master_byteenable           => CONNECTED_TO_hdmi_sync_master_byteenable,           --                               .byteenable
			hdmi_sync_master_debugaccess          => CONNECTED_TO_hdmi_sync_master_debugaccess           --                               .debugaccess
		);

