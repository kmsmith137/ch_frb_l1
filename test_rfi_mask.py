import simulate_l0

l0 = simulate_l0.l0sim('l0_configs/l0_rfi.yml', 1.0)
print(dir(l0))

l0.send_chunk_file(0, 'chunk-0000-000000000000.msgpack')

