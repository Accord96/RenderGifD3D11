#pragma once
HWND g_hwnd = NULL;

ID3D11Device* g_dev = NULL;
ID3D11DeviceContext* g_ctx = NULL;
IDXGISwapChain* g_sc = NULL;
ID3D11RenderTargetView* g_rtv = NULL;

ID3D11VertexShader* g_vs = NULL;
ID3D11PixelShader* g_ps = NULL;
ID3D11InputLayout* g_il = NULL;
ID3D11Buffer* g_vb = NULL;
ID3D11Buffer* g_ib = NULL;
ID3D11Buffer* g_cb = NULL;
ID3D11SamplerState* g_samp = NULL;
ID3D11BlendState* g_blend = NULL;
ID3D11RasterizerState* g_rs = NULL;
ID3D11DepthStencilState* g_dss = NULL;

struct VS_CB
{
    float ScreenW;
    float ScreenH;
    float PosX;
    float PosY;
    float SizeW;
    float SizeH;
    float _pad0;
    float _pad1;
};

struct Vtx
{
    float pos[2];
    float uv[2];
};

const Vtx g_kQuad[4] = 
{
    { {0,0}, {0,0} },
    { {1,0}, {1,0} },
    { {1,1}, {1,1} },
    { {0,1}, {0,1} },
};

const uint16_t g_kIdx[6] = 
{
    0,1,2, 
    0,2,3 
};

VOID CreateRTV()
{
    ID3D11Texture2D* bb = NULL;
    g_sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_dev->CreateRenderTargetView(bb, NULL, &g_rtv);
    if (bb)
        bb->Release();
}

VOID CleanupRTV()
{
    if (g_rtv)
        g_rtv->Release();

    g_rtv = NULL;
}

BOOL CreateDeviceD3D(
    HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    memset(&sd, 0, sizeof(sd));

    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL flOut;
    D3D_FEATURE_LEVEL flReq[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = 0;

    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        flags, flReq, 2, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev, &flOut, &g_ctx) != S_OK)
        return FALSE;

    CreateRTV();
    return TRUE;
}

BOOL CreateD3D11Render()
{

    const char* VS_SRC = R"(
    cbuffer CB : register(b0)
    {
        float ScreenW;
        float ScreenH;
        float PosX;
        float PosY;
        float SizeW;
        float SizeH;
        float _pad0;
        float _pad1;
    };
    struct VS_IN { float2 pos:POSITION; float2 uv:TEXCOORD0; };
    struct VS_OUT{ float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
    VS_OUT main(VS_IN i)
    {
        float2 px = float2(PosX, PosY) + i.pos * float2(SizeW, SizeH);
        float2 ndc;
        ndc.x = (px.x / ScreenW) * 2.0f - 1.0f;
        ndc.y = 1.0f - (px.y / ScreenH) * 2.0f;
        VS_OUT o;
        o.pos = float4(ndc, 0.0f, 1.0f);
        o.uv  = i.uv;
        return o;
    }
    )";

    const char* PS_SRC = R"(
    Texture2D tex0 : register(t0);
    SamplerState smp0 : register(s0);
    struct PS_IN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
    float4 main(PS_IN i) : SV_Target
    {
        float4 c = tex0.Sample(smp0, i.uv);
        return c;
    }
    )";

    ID3DBlob* vsb = NULL; 
    ID3DBlob* err = NULL;

    if (FAILED(D3DCompile(VS_SRC, strlen(VS_SRC), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vsb, &err)))
        return FALSE;

    if (FAILED(g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), NULL, &g_vs)))
    {
        vsb->Release();
        return FALSE;
    }

    D3D11_INPUT_ELEMENT_DESC il[] = 
    {
        { "POSITION",0,DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(Vtx, pos), D3D11_INPUT_PER_VERTEX_DATA,0 },
        { "TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(Vtx, uv), D3D11_INPUT_PER_VERTEX_DATA,0 },
    };

    if (FAILED(g_dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_il)))
    {
        vsb->Release();
        return FALSE;
    }
    vsb->Release();

    ID3DBlob* psb = NULL;
    if (FAILED(D3DCompile(PS_SRC, strlen(PS_SRC), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &psb, &err))) 
        return FALSE;

    if (FAILED(g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), NULL, &g_ps))) 
    {
        psb->Release();
        return FALSE;
    }
    psb->Release();

    D3D11_BUFFER_DESC bd;
    memset(&bd, 0, sizeof(bd));

    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = sizeof(g_kQuad);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    
    D3D11_SUBRESOURCE_DATA srd;
    memset(&srd, 0, sizeof(srd));
    srd.pSysMem = g_kQuad;

    if (FAILED(g_dev->CreateBuffer(&bd, &srd, &g_vb)))
        return FALSE;

    D3D11_BUFFER_DESC id;
    memset(&id, 0, sizeof(id));

    id.BindFlags = D3D11_BIND_INDEX_BUFFER;
    id.ByteWidth = sizeof(g_kIdx);
    id.Usage = D3D11_USAGE_IMMUTABLE;

    D3D11_SUBRESOURCE_DATA isrd;
    memset(&isrd, 0, sizeof(isrd));
    isrd.pSysMem = g_kIdx;

    if (FAILED(g_dev->CreateBuffer(&id, &isrd, &g_ib)))
        return FALSE;

    D3D11_BUFFER_DESC cbd;
    memset(&cbd, 0, sizeof(cbd));

    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.ByteWidth = sizeof(VS_CB);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_dev->CreateBuffer(&cbd, NULL, &g_cb))) 
        return FALSE;

    D3D11_SAMPLER_DESC sd;
    memset(&sd, 0, sizeof(sd));

    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g_dev->CreateSamplerState(&sd, &g_samp))) 
        return FALSE;

    D3D11_BLEND_DESC b;
    memset(&b, 0, sizeof(b));

    b.RenderTarget[0].BlendEnable = TRUE;
    b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g_dev->CreateBlendState(&b, &g_blend)))
        return FALSE;

    D3D11_RASTERIZER_DESC r;
    memset(&r, 0, sizeof(r));

    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.ScissorEnable = FALSE;
    r.DepthClipEnable = TRUE;
    if (FAILED(g_dev->CreateRasterizerState(&r, &g_rs))) 
        return FALSE;

    D3D11_DEPTH_STENCIL_DESC ds;
    memset(&ds, 0, sizeof(ds));

    ds.DepthEnable = FALSE;
    if (FAILED(g_dev->CreateDepthStencilState(&ds, &g_dss)))
        return FALSE;

    return TRUE;
}

VOID DestroyD3D11Render()
{
    if (g_dss)
        g_dss->Release();
    if (g_rs)
        g_rs->Release();
    if (g_blend)
        g_blend->Release();
    if (g_samp)
        g_samp->Release();
    if (g_cb)
        g_cb->Release();
    if (g_ib)
        g_ib->Release();
    if (g_vb)
        g_vb->Release();
    if (g_il)
        g_il->Release();    
    if (g_ps)
        g_ps->Release();   
    if (g_vs)
        g_vs->Release();

    g_dss = NULL;
    g_rs = NULL;
    g_blend = NULL;
    g_samp = NULL;
    g_cb = NULL;
    g_ib = NULL;
    g_vb = NULL;
    g_il = NULL;
    g_ps = NULL;
    g_vs = NULL;
}

VOID Resize(
    UINT w, 
    UINT h)
{
    if (!g_sc) 
        return;
    g_ctx->OMSetRenderTargets(0, NULL, NULL);
    CleanupRTV();
    g_sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    CreateRTV();
}

VOID DestroyDeviceD3D()
{
    CleanupRTV();
    if (g_sc)
        g_sc->Release();
    if (g_ctx)
        g_ctx->Release();
    if (g_dev)
        g_dev->Release();

    g_sc = NULL;
    g_ctx = NULL;
    g_dev = NULL;
}