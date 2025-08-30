#pragma once

typedef struct _DATA_GIF_DX11
{
    int w;
    int h;
    int z;
    int cur;
    float acc;
    uint8_t* framesRGBA;
    int* delaysMs;
    ID3D11Texture2D* tex;
    ID3D11ShaderResourceView* srv;
    BOOL finished;

} DATA_GIF_DX11;

DATA_GIF_DX11 g_gifStarting;
DATA_GIF_DX11 g_gifLoading;

VOID ImGuiGifDX11_Reset(
    DATA_GIF_DX11* g)
{
    g->w = g->h = g->z = g->cur = 0;
    g->acc = 0.0f;

    if (g->framesRGBA)
    {
        free(g->framesRGBA);
        g->framesRGBA = NULL;
    }
    if (g->delaysMs)
    {
        free(g->delaysMs);
        g->delaysMs = NULL;
    }
    if (g->srv)
    {
        g->srv->Release();
        g->srv = NULL;
    }
    if (g->tex)
    {
        g->tex->Release();
        g->tex = NULL;
    }
}

VOID UploadFrameGif(
    DATA_GIF_DX11* g, 
    ID3D11DeviceContext* ctx, 
    int index)
{
    size_t frameStride = (size_t)g->w * g->h * 4;
    uint8_t* src = g->framesRGBA + frameStride * index;

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx->Map((ID3D11Resource*)g->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        uint8_t* s = src;
        uint8_t* d = (uint8_t*)m.pData;
        for (int row = 0; row < g->h; ++row)
        {
            memcpy(d, s, (size_t)g->w * 4);
            s += (size_t)g->w * 4;
            d += m.RowPitch;
        }
        ctx->Unmap((ID3D11Resource*)g->tex, 0);
    }
}

BOOL LoadGif(
    DATA_GIF_DX11* g, 
    ID3D11Device* dev, 
    ID3D11DeviceContext* ctx,
    const void* data,
    int size)
{
    ImGuiGifDX11_Reset(g);

    int* delays = NULL;
    int comp = 0, zz = 0, x = 0, y = 0;

    unsigned char* pixels = stbi_load_gif_from_memory(
        (const unsigned char*)data, size,
        &delays, &x, &y, &zz, &comp, 4);

    if (!pixels || zz <= 0 || x <= 0 || y <= 0) 
    {
        if (pixels)
            free(pixels);
        if (delays)
            free(delays);
        return FALSE;
    }

    g->w = x; 
    g->h = y;
    g->z = zz;

    size_t frameStride = (size_t)g->w * g->h * 4;
    g->framesRGBA = (uint8_t*)malloc(frameStride * g->z);
    if (!g->framesRGBA)
    {
        free(pixels);
        free(delays);
        return FALSE;
    }

    memcpy(g->framesRGBA, pixels, frameStride * g->z);
    free(pixels);

    g->delaysMs = (int*)malloc(sizeof(int) * g->z);
    if (!g->delaysMs)
    {
        free(g->framesRGBA);
        g->framesRGBA = NULL; 
        free(delays);
        return FALSE;
    }
    memcpy(g->delaysMs, delays, sizeof(int) * g->z);
    free(delays);

    for (int f = 0; f < g->z; ++f)
    {
        uint8_t* p = g->framesRGBA + frameStride * (size_t)f;
        for (size_t i = 0; i < frameStride; i += 4)
        {
            if (p[i + 0] <= 16 && p[i + 1] <= 16 && p[i + 2] <= 16) 
            {
                p[i + 0] = 0; p[i + 1] = 0; p[i + 2] = 0; p[i + 3] = 0;
            }
        }
    }

    for (int i = 0; i < g->z; ++i) 
    {
        if (g->delaysMs[i] <= 0) 
            g->delaysMs[i] = 100;
    }

    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));

    td.Width = (UINT)g->w;
    td.Height = (UINT)g->h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(dev->CreateTexture2D(&td, NULL, &g->tex))) 
        return FALSE;
    if (FAILED(dev->CreateShaderResourceView((ID3D11Resource*)g->tex, NULL, &g->srv))) 
        return FALSE;

    UploadFrameGif(g, ctx, 0);
    g->cur = 0;
    g->acc = 0.0f;

    return TRUE;
}

VOID UpdateGif(
    DATA_GIF_DX11* g, 
    ID3D11DeviceContext* ctx,
    float dt,
    BOOL whileGif)
{
    if (g->z <= 1 || g->finished) 
        return;

    g->acc += dt;
    float curDelay = g->delaysMs[g->cur] * 0.001f;
    if (g->acc < curDelay)
        return;

    do
    {
        g->acc -= curDelay;
        g->cur++;
        if (g->cur >= g->z)
        {
            if (whileGif)
            {
                g->cur = 0;
            }
            else
            {
                g->cur = g->z - 1;
                g->finished = 1;
                break;
            }
        }
        curDelay = g->delaysMs[g->cur] * 0.001f;
    } while (g->acc >= curDelay);

    if (!g->finished)
    {
        UploadFrameGif(g, ctx, g->cur);
    }
}

BOOL RenderGif(
    DATA_GIF_DX11* g,
    float dt,
    BOOL whileGif)
{
    UpdateGif(g, g_ctx, dt, whileGif);

    const float clear[4] = { 0.08f, 0.12f, 0.18f, 1.0f };
    g_ctx->OMSetRenderTargets(1, &g_rtv, NULL);
    g_ctx->ClearRenderTargetView(g_rtv, clear);

    if (g->srv)
    {
        RECT rc; 
        GetClientRect(g_hwnd, &rc);
        
        float sw = float(rc.right - rc.left);
        float sh = float(rc.bottom - rc.top);

        float px = 00.0f;
        float py = 00.0f;
        float pw = (float)g->w;
        float ph = (float)g->h;

        D3D11_VIEWPORT vp;
        memset(&vp, 0, sizeof(vp));

        vp.Width = sw;
        vp.Height = sh;
        vp.MinDepth = 0.f;
        vp.MaxDepth = 1.f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;

        g_ctx->RSSetViewports(1, &vp);

        g_ctx->IASetInputLayout(g_il);
        UINT stride = sizeof(Vtx), offset = 0;
        g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &offset);
        g_ctx->IASetIndexBuffer(g_ib, DXGI_FORMAT_R16_UINT, 0);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->RSSetState(g_rs);
        g_ctx->OMSetDepthStencilState(g_dss, 0);
        const float bf[4] = { 0, 0, 0,0 };
        g_ctx->OMSetBlendState(g_blend, bf, 0xFFFFFFFF);

        D3D11_MAPPED_SUBRESOURCE m;
        memset(&m, 0, sizeof(m));

        if (SUCCEEDED(g_ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            VS_CB* cb = (VS_CB*)m.pData;
            cb->ScreenW = sw; cb->ScreenH = sh;
            cb->PosX = px;   cb->PosY = py;
            cb->SizeW = pw;  cb->SizeH = ph;
            cb->_pad0 = cb->_pad1 = 0.f;
            g_ctx->Unmap(g_cb, 0);
        }

        g_ctx->VSSetShader(g_vs, NULL, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
        g_ctx->PSSetShader(g_ps, NULL, 0);
        ID3D11ShaderResourceView* srv = g->srv;
        g_ctx->PSSetShaderResources(0, 1, &srv);
        g_ctx->PSSetSamplers(0, 1, &g_samp);
        g_ctx->DrawIndexed(6, 0, 0);

        ID3D11ShaderResourceView* null_srv = NULL;
        g_ctx->PSSetShaderResources(0, 1, &null_srv);

        if (g->finished)
            return TRUE;
    }

    return FALSE;
}
