mod app;
mod meta;
mod ring;
mod xdp;

macro_rules! max_len {
    ($arr:expr) => {{
        #[allow(clippy::cast_possible_truncation)]
        {
            let arr = $arr;
            let (mut max, mut i) = (0, 0);

            while i < arr.len() {
                if arr[i].len() > max {
                    max = arr[i].len();
                }
                i += 1;
            }

            max as u16
        }
    }};
}

pub(super) use max_len;
