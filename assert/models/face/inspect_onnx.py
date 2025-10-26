import sys, math
import onnx
from onnx import numpy_helper, TensorProto

DTYPE_MAP = {
	TensorProto.FLOAT: "float32",
	TensorProto.UINT8: "uint8",
	TensorProto.INT8: "int8",
	TensorProto.UINT16: "uint16",
	TensorProto.INT16: "int16",
	TensorProto.INT32: "int32",
	TensorProto.INT64: "int64",
	TensorProto.BOOL: "bool",
	TensorProto.FLOAT16: "float16",
	TensorProto.DOUBLE: "float64",
}

def dim_to_str(d):
	#d.dim_value: 정수, d.dim_param: 동적 심볼
	if d.HasField("dim_value"):
		return str(d.dim_value)
	if d.HasField("dim_param") and d.dim_param:
		return d.sim_param
	return "?"

def value_info_to_spec(vi):
	t = vi.type.tensor_type
	dtype = DTYPE_MAP.get(t.elem_type, f"unk({t.elem_type})")
	shape = [dim_to_str(d) for d in t.shape.dim]
	return dtype, shape

def list_non_initializer_inputs(model):
    # 초기화 파라미터(가중치) 이름들
    init_names = set(i.name for i in model.graph.initializer)
    # 진짜 그래프 입력(데이터 텐서)만 골라냄
    real_inputs = [i for i in model.graph.input if i.name not in init_names]
    return real_inputs

def find_constants(model, targets=(127.5,128.0), eps=1e-3):
    hits = []
    for init in model.graph.initializer:
        arr = numpy_helper.to_array(init)
        if arr.size == 1:
            v = float(arr.flatten()[0])
            for t in targets:
                if math.isfinite(v) and abs(v - t) <= eps:
                    hits.append((init.name, v))
    return hits

def main(path):
    model = onnx.load(path)
    onnx.checker.check_model(model)

    ir_version = model.ir_version
    opsets = {op.domain or "ai.onnx": op.version for op in model.opset_import}
    print("=== ONNX MODEL INFO ===")
    print(f"file        : {path}")
    print(f"ir_version  : {ir_version}")
    print("opsets      :", ", ".join([f"{k}={v}" for k,v in opsets.items()]))

    # 입력(가중치 제외)
    inputs = list_non_initializer_inputs(model)
    print("\n--- INPUTS (data tensors) ---")
    if not inputs:
        print("(no data inputs found; model may use dynamic IO or all inputs folded)")
    for i, vi in enumerate(inputs):
        dtype, shape = value_info_to_spec(vi)
        print(f"[{i}] name={vi.name}  dtype={dtype}  shape={shape}")

    # 출력
    print("\n--- OUTPUTS ---")
    for i, vo in enumerate(model.graph.output):
        dtype, shape = value_info_to_spec(vo)
        print(f"[{i}] name={vo.name}  dtype={dtype}  shape={shape}")

    # 간단한 노드/그래프 요약
    print("\n--- GRAPH SUMMARY ---")
    print(f"nodes: {len(model.graph.node)}  initializers: {len(model.graph.initializer)}")

    # 전처리 힌트(127.5, 128.0 상수 탐지)
    hits = find_constants(model, (127.5,128.0))
    if hits:
        print("\n--- PREPROCESS HINTS ---")
        for name, v in hits:
            print(f"const initializer near preprocessing: {name} ≈ {v}")
        print("※ (img - 127.5) / 128 형태의 전처리가 모델 내부에 포함됐을 가능성이 있습니다.")
    else:
        print("\n--- PREPROCESS HINTS ---")
        print("전형적인 127.5/128 상수는 발견되지 않았습니다. 전처리는 모델 외부에서 하시는 듯합니다.")

    # 임베딩 차원 추정(출력 shape로)
    try:
        out = model.graph.output[0]
        _, osh = value_info_to_spec(out)
        # 일반적으로 [N, D] 또는 [D]
        D = None
        if osh:
            # 뒤에서부터 첫 번째 정수 차원을 임베딩 차원으로 추정
            for x in reversed(osh):
                if x.isdigit():
                    D = int(x); break
        if D:
            print(f"\n--- EMBEDDING GUESS ---\nembedding dimension (guess) : {D}")
    except Exception:
        pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 inspect_onnx.py <model.onnx>")
        sys.exit(1)
    main(sys.argv[1])
