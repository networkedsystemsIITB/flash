use std::hash::{BuildHasher, BuildHasherDefault, Hash};

use fnv::FnvHasher;
use twox_hash::XxHash3_64;

type H1 = BuildHasherDefault<FnvHasher>;
type H2 = BuildHasherDefault<XxHash3_64>;

pub struct Maglev<H: BuildHasher> {
    lookup_table: Vec<usize>,
    hasher: H,
}

impl<H: BuildHasher + Default> Maglev<H> {
    #[allow(non_snake_case)]
    pub fn new<T: Hash>(nodes: &[T], size: usize) -> Self {
        let N = nodes.len();
        let M = size;

        let permutations = generate_permutations(nodes, M);
        let mut next = vec![0; N];
        let mut lookup_table = vec![N; M];

        let mut n = 0;

        'outer: loop {
            for i in 0..N {
                let mut c = permutations[i][next[i]];
                while lookup_table[c] != N {
                    next[i] += 1;
                    c = permutations[i][next[i]];
                }

                lookup_table[c] = i;
                next[i] += 1;
                n += 1;

                if n == M {
                    break 'outer;
                }
            }
        }

        Maglev {
            lookup_table,
            hasher: H::default(),
        }
    }

    #[allow(clippy::cast_possible_truncation)]
    #[inline]
    pub fn lookup<T: Hash>(&self, key: &T) -> usize {
        self.lookup_table[(self.hasher.hash_one(key) % self.lookup_table.len() as u64) as usize]
    }
}

#[allow(clippy::cast_possible_truncation)]
fn generate_permutations<T: Hash>(nodes: &[T], size: usize) -> Vec<Vec<usize>> {
    let h1 = H1::default();
    let h2 = H2::default();

    nodes
        .iter()
        .map(|t| {
            (
                (h1.hash_one(t) as usize) % size,
                (h2.hash_one(t) as usize) % (size - 1) + 1,
            )
        })
        .map(|(offset, skip)| (0..size).map(|i| (offset + i * skip) % size).collect())
        .collect()
}
