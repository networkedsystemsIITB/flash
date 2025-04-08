macro_rules! flash_command {
    ($name:ident, $value:expr) => {
        pub(super) const $name: [u8; 4] = ($value as u32).to_ne_bytes();
    };
}

// flash_command!(FLASH_CREATE_UMEM, 1);
flash_command!(FLASH_GET_UMEM, 2);
flash_command!(FLASH_CREATE_SOCKET, 3);
flash_command!(FLASH_CLOSE_CONN, 4);
// flash_command!(FLASH_GET_THREAD_INFO, 5);
flash_command!(FLASH_GET_UMEM_OFFSET, 6);
flash_command!(FLASH_GET_ROUTE_INFO, 7);
flash_command!(FLASH_GET_BIND_FLAGS, 8);
flash_command!(FLASH_GET_XDP_FLAGS, 9);
flash_command!(FLASH_GET_MODE, 10);
flash_command!(FLASH_GET_POLL_TIMEOUT, 11);
flash_command!(FLASH_GET_FRAGS_ENABLED, 12);
flash_command!(FLASH_GET_IFNAME, 13);
flash_command!(FLASH_GET_IP_ADDR, 14);
flash_command!(FLASH_GET_DST_IP_ADDR, 15);
