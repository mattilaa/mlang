#include <math.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

static mlang_list_t empty_list(void)
{
    mlang_list_t out;
    out.size = 0;
    out.data = NULL;
    return out;
}

int32_t __mlang_std_fft_is_power_of_two(int64_t n)
{
    if(n <= 0)
        return 0;
    return (n & (n - 1)) == 0 ? 1 : 0;
}

static int fft_inplace(double* re, double* im, int64_t n, int inverse)
{
    if(!re || !im || n <= 0)
        return 0;

    int64_t j = 0;
    for(int64_t i = 1; i < n; ++i)
    {
        int64_t bit = n >> 1;
        while(j & bit)
        {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if(i < j)
        {
            double tr = re[i];
            re[i] = re[j];
            re[j] = tr;
            double ti = im[i];
            im[i] = im[j];
            im[j] = ti;
        }
    }

    const double pi = 3.14159265358979323846;
    for(int64_t len = 2; len <= n; len <<= 1)
    {
        double ang = (inverse ? 2.0 : -2.0) * pi / (double)len;
        double wlen_r = cos(ang);
        double wlen_i = sin(ang);

        for(int64_t i = 0; i < n; i += len)
        {
            double wr = 1.0;
            double wi = 0.0;
            int64_t half = len >> 1;
            for(int64_t k = 0; k < half; ++k)
            {
                int64_t u = i + k;
                int64_t v = u + half;

                double vr = re[v] * wr - im[v] * wi;
                double vi = re[v] * wi + im[v] * wr;
                double ur = re[u];
                double ui = im[u];

                re[u] = ur + vr;
                im[u] = ui + vi;
                re[v] = ur - vr;
                im[v] = ui - vi;

                double next_wr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
            }
        }
    }

    if(inverse)
    {
        double inv_n = 1.0 / (double)n;
        for(int64_t i = 0; i < n; ++i)
        {
            re[i] *= inv_n;
            im[i] *= inv_n;
        }
    }

    return 1;
}

static mlang_list_t fft_run_i64(mlang_list_t data, int inverse)
{
    if(data.size <= 0 || (data.size % 2) != 0 || !data.data)
        return empty_list();

    int64_t n = data.size / 2;
    if(__mlang_std_fft_is_power_of_two(n) == 0)
        return empty_list();

    int64_t* src = (int64_t*)data.data;
    double* re = (double*)malloc(sizeof(double) * (size_t)n);
    double* im = (double*)malloc(sizeof(double) * (size_t)n);
    if(!re || !im)
    {
        free(re);
        free(im);
        return empty_list();
    }

    for(int64_t i = 0; i < n; ++i)
    {
        re[i] = (double)src[2 * i];
        im[i] = (double)src[2 * i + 1];
    }

    if(!fft_inplace(re, im, n, inverse))
    {
        free(re);
        free(im);
        return empty_list();
    }

    int64_t* out_data = (int64_t*)malloc(sizeof(int64_t) * (size_t)data.size);
    if(!out_data)
    {
        free(re);
        free(im);
        return empty_list();
    }

    for(int64_t i = 0; i < n; ++i)
    {
        out_data[2 * i] = (int64_t)llround(re[i]);
        out_data[2 * i + 1] = (int64_t)llround(im[i]);
    }

    free(re);
    free(im);

    mlang_list_t out;
    out.size = data.size;
    out.data = out_data;
    return out;
}

mlang_list_t __mlang_std_fft_forward_i64(mlang_list_t data)
{
    return fft_run_i64(data, 0);
}

mlang_list_t __mlang_std_fft_inverse_i64(mlang_list_t data)
{
    return fft_run_i64(data, 1);
}
