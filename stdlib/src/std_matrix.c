#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int matrix_pivot(double* matrix, int64_t size, int64_t column,
                        int64_t* pivot_row)
{
    double largest = 0.0;
    *pivot_row = column;
    for(int64_t row = column; row < size; ++row)
    {
        double value = fabs(matrix[row * size + column]);
        if(value > largest)
        {
            largest = value;
            *pivot_row = row;
        }
    }
    return largest > 1.0e-12;
}

static void matrix_swap_rows(double* matrix, int64_t size, int64_t first,
                             int64_t second)
{
    if(first == second)
        return;
    for(int64_t column = 0; column < size; ++column)
    {
        double value = matrix[first * size + column];
        matrix[first * size + column] = matrix[second * size + column];
        matrix[second * size + column] = value;
    }
}

static double matrix_determinant(const double* input, int64_t size)
{
    if(size <= 0)
        return 0.0;
    double* matrix = malloc((size_t)(size * size) * sizeof(double));
    if(!matrix)
        return 0.0;
    memcpy(matrix, input, (size_t)(size * size) * sizeof(double));

    double determinant = 1.0;
    for(int64_t column = 0; column < size; ++column)
    {
        int64_t pivot_row;
        if(!matrix_pivot(matrix, size, column, &pivot_row))
        {
            free(matrix);
            return 0.0;
        }
        if(pivot_row != column)
        {
            matrix_swap_rows(matrix, size, pivot_row, column);
            determinant = -determinant;
        }
        double pivot = matrix[column * size + column];
        determinant *= pivot;
        for(int64_t row = column + 1; row < size; ++row)
        {
            double factor = matrix[row * size + column] / pivot;
            for(int64_t index = column + 1; index < size; ++index)
                matrix[row * size + index] -=
                    factor * matrix[column * size + index];
        }
    }
    free(matrix);
    return determinant;
}

static int32_t matrix_inverse(const double* input, double* output,
                              int64_t size)
{
    if(size <= 0)
        return 0;
    double* matrix = malloc((size_t)(size * size) * sizeof(double));
    if(!matrix)
        return 0;
    memcpy(matrix, input, (size_t)(size * size) * sizeof(double));
    memset(output, 0, (size_t)(size * size) * sizeof(double));
    for(int64_t row = 0; row < size; ++row)
        output[row * size + row] = 1.0;

    for(int64_t column = 0; column < size; ++column)
    {
        int64_t pivot_row;
        if(!matrix_pivot(matrix, size, column, &pivot_row))
        {
            free(matrix);
            return 0;
        }
        matrix_swap_rows(matrix, size, pivot_row, column);
        matrix_swap_rows(output, size, pivot_row, column);

        double pivot = matrix[column * size + column];
        for(int64_t index = 0; index < size; ++index)
        {
            matrix[column * size + index] /= pivot;
            output[column * size + index] /= pivot;
        }
        for(int64_t row = 0; row < size; ++row)
        {
            if(row == column)
                continue;
            double factor = matrix[row * size + column];
            for(int64_t index = 0; index < size; ++index)
            {
                matrix[row * size + index] -=
                    factor * matrix[column * size + index];
                output[row * size + index] -=
                    factor * output[column * size + index];
            }
        }
    }
    free(matrix);
    return 1;
}

static int32_t matrix_eigen_symmetric(const double* input, double* values,
                                      double* vectors, int64_t size)
{
    if(size <= 0)
        return 0;
    double* matrix = malloc((size_t)(size * size) * sizeof(double));
    if(!matrix)
        return 0;
    memcpy(matrix, input, (size_t)(size * size) * sizeof(double));
    memset(vectors, 0, (size_t)(size * size) * sizeof(double));
    for(int64_t row = 0; row < size; ++row)
        vectors[row * size + row] = 1.0;

    for(int64_t row = 0; row < size; ++row)
    {
        for(int64_t column = row + 1; column < size; ++column)
        {
            double left = matrix[row * size + column];
            double right = matrix[column * size + row];
            double scale = fmax(1.0, fmax(fabs(left), fabs(right)));
            if(fabs(left - right) > 1.0e-10 * scale)
            {
                free(matrix);
                return -1;
            }
        }
    }

    int64_t max_iterations = 100 * size * size;
    int converged = size == 1;
    for(int64_t iteration = 0; iteration < max_iterations && !converged;
        ++iteration)
    {
        int64_t pivot_row = 0;
        int64_t pivot_column = 1;
        double largest = 0.0;
        for(int64_t row = 0; row < size; ++row)
        {
            for(int64_t column = row + 1; column < size; ++column)
            {
                double value = fabs(matrix[row * size + column]);
                if(value > largest)
                {
                    largest = value;
                    pivot_row = row;
                    pivot_column = column;
                }
            }
        }
        if(largest <= 1.0e-12)
        {
            converged = 1;
            break;
        }

        double app = matrix[pivot_row * size + pivot_row];
        double aqq = matrix[pivot_column * size + pivot_column];
        double apq = matrix[pivot_row * size + pivot_column];
        double angle = 0.5 * atan2(2.0 * apq, aqq - app);
        double cosine = cos(angle);
        double sine = sin(angle);

        for(int64_t index = 0; index < size; ++index)
        {
            if(index == pivot_row || index == pivot_column)
                continue;
            double aip = matrix[index * size + pivot_row];
            double aiq = matrix[index * size + pivot_column];
            double new_ip = cosine * aip - sine * aiq;
            double new_iq = sine * aip + cosine * aiq;
            matrix[index * size + pivot_row] = new_ip;
            matrix[pivot_row * size + index] = new_ip;
            matrix[index * size + pivot_column] = new_iq;
            matrix[pivot_column * size + index] = new_iq;
        }
        matrix[pivot_row * size + pivot_row] =
            cosine * cosine * app - 2.0 * sine * cosine * apq +
            sine * sine * aqq;
        matrix[pivot_column * size + pivot_column] =
            sine * sine * app + 2.0 * sine * cosine * apq +
            cosine * cosine * aqq;
        matrix[pivot_row * size + pivot_column] = 0.0;
        matrix[pivot_column * size + pivot_row] = 0.0;

        for(int64_t row = 0; row < size; ++row)
        {
            double vip = vectors[row * size + pivot_row];
            double viq = vectors[row * size + pivot_column];
            vectors[row * size + pivot_row] = cosine * vip - sine * viq;
            vectors[row * size + pivot_column] = sine * vip + cosine * viq;
        }
    }
    if(!converged)
    {
        free(matrix);
        return 0;
    }

    for(int64_t index = 0; index < size; ++index)
        values[index] = matrix[index * size + index];
    for(int64_t first = 0; first < size; ++first)
    {
        int64_t smallest = first;
        for(int64_t second = first + 1; second < size; ++second)
            if(values[second] < values[smallest])
                smallest = second;
        if(smallest == first)
            continue;
        double value = values[first];
        values[first] = values[smallest];
        values[smallest] = value;
        for(int64_t row = 0; row < size; ++row)
        {
            value = vectors[row * size + first];
            vectors[row * size + first] = vectors[row * size + smallest];
            vectors[row * size + smallest] = value;
        }
    }
    free(matrix);
    return 1;
}

double __mlang_std_matrix_determinant_f64(const double* input, int64_t size)
{
    return matrix_determinant(input, size);
}

float __mlang_std_matrix_determinant_f32(const float* input, int64_t size)
{
    double* converted = malloc((size_t)(size * size) * sizeof(double));
    if(!converted)
        return 0.0f;
    for(int64_t index = 0; index < size * size; ++index)
        converted[index] = input[index];
    float result = (float)matrix_determinant(converted, size);
    free(converted);
    return result;
}

int32_t __mlang_std_matrix_inverse_f64(const double* input, double* output,
                                       int64_t size)
{
    return matrix_inverse(input, output, size);
}

int32_t __mlang_std_matrix_inverse_f32(const float* input, float* output,
                                       int64_t size)
{
    double* converted = malloc((size_t)(size * size) * sizeof(double));
    double* result = malloc((size_t)(size * size) * sizeof(double));
    if(!converted || !result)
    {
        free(converted);
        free(result);
        return 0;
    }
    for(int64_t index = 0; index < size * size; ++index)
        converted[index] = input[index];
    int32_t status = matrix_inverse(converted, result, size);
    if(status)
        for(int64_t index = 0; index < size * size; ++index)
            output[index] = (float)result[index];
    free(converted);
    free(result);
    return status;
}

int32_t __mlang_std_matrix_eigen_symmetric_f64(const double* input,
                                                double* values,
                                                double* vectors, int64_t size)
{
    return matrix_eigen_symmetric(input, values, vectors, size);
}

int32_t __mlang_std_matrix_eigen_symmetric_f32(const float* input,
                                                float* values, float* vectors,
                                                int64_t size)
{
    double* converted = malloc((size_t)(size * size) * sizeof(double));
    double* result_values = malloc((size_t)size * sizeof(double));
    double* result_vectors = malloc((size_t)(size * size) * sizeof(double));
    if(!converted || !result_values || !result_vectors)
    {
        free(converted);
        free(result_values);
        free(result_vectors);
        return 0;
    }
    for(int64_t index = 0; index < size * size; ++index)
        converted[index] = input[index];
    int32_t status = matrix_eigen_symmetric(converted, result_values,
                                            result_vectors, size);
    if(status > 0)
    {
        for(int64_t index = 0; index < size; ++index)
            values[index] = (float)result_values[index];
        for(int64_t index = 0; index < size * size; ++index)
            vectors[index] = (float)result_vectors[index];
    }
    free(converted);
    free(result_values);
    free(result_vectors);
    return status;
}
