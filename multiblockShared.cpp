/**
 * Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

 /**
  * Matrix multiplication: C = A * B.
  * Host code.
  *
  * This sample implements matrix multiplication which makes use of shared memory
  * to ensure data reuse, the matrix multiplication is done using tiling approach.
  * It has been written for clarity of exposition to illustrate various CUDA programming
  * principles, not with the goal of providing the most performant generic kernel for matrix multiplication.
  * See also:
  * V. Volkov and J. Demmel, "Benchmarking GPUs to tune dense linear algebra,"
  * in Proc. 2008 ACM/IEEE Conf. on Supercomputing (SC '08),
  * Piscataway, NJ: IEEE Press, 2008, pp. Art. 31:1-11.
  */

  // System includes
#include <stdio.h>
#include <assert.h>

// CUDA runtime
#include <cuda_runtime.h>

// Helper functions and utilities to work with CUDA
#include <helper_functions.h>
#include <helper_cuda.h>

/**
 * Matrix multiplication (CUDA Kernel) on the device: C = A * B
 * wA is A's width and wB is B's width
 */
template <int BLOCK_SIZE> __global__ void MatrixMulCUDA(float *C, float *A,
	float *B, int wA,
	int wB) {
	// Block index
	int bx = blockIdx.x;
	int by = blockIdx.y;

	// Thread index
	int tx = threadIdx.x;
	int ty = threadIdx.y;

	int row = by * blockDim.y + ty;
	int col = bx * blockDim.x + tx;
	float C_local = 0;

	__shared__ float Ads[BLOCK_SIZE][BLOCK_SIZE];
	__shared__ float Bds[BLOCK_SIZE][BLOCK_SIZE];
	for (int m = 0; m < wA / BLOCK_SIZE; ++m) {
		Ads[tx][ty] = A[row * wA + m*BLOCK_SIZE + tx];
		Bds[tx][ty] = B[(m*BLOCK_SIZE + ty)*wA + col];
		__syncthreads();
		for (int k = 0; k < BLOCK_SIZE; ++k)
			C_local += Ads[tx][k] * Bds[k][ty];
		__syncthreads();
	}

	C[row*wA + col] = C_local;
}

void ConstantInit(float *data, int size, float val) {
	for (int i = 0; i < size; ++i) {
		data[i] = val;
	}
}

/**
 * Run a simple test of matrix multiplication using CUDA
 */
int MatrixMultiply(int argc, char **argv,
	int block_size, const dim3 &dimsA,
	const dim3 &dimsB) {
	// Allocate host memory for matrices A and B
	unsigned int size_A = dimsA.x * dimsA.y;
	unsigned int mem_size_A = sizeof(float) * size_A;
	float *h_A = reinterpret_cast<float *>(malloc(mem_size_A));
	unsigned int size_B = dimsB.x * dimsB.y;
	unsigned int mem_size_B = sizeof(float) * size_B;
	float *h_B = reinterpret_cast<float *>(malloc(mem_size_B));

	// Initialize host memory
	const float valB = 0.01f;
	ConstantInit(h_A, size_A, 1.0f);
	ConstantInit(h_B, size_B, valB);

	// Allocate device memory
	float *d_A, *d_B, *d_C;

	// Allocate host matrix C
	dim3 dimsC(dimsB.x, dimsA.y, 1);
	unsigned int mem_size_C = dimsC.x * dimsC.y * sizeof(float);
	float *h_C = reinterpret_cast<float *>(malloc(mem_size_C));

	if (h_C == NULL) {
		fprintf(stderr, "Failed to allocate host matrix C!\n");
		exit(EXIT_FAILURE);
	}

	checkCudaErrors(cudaMalloc(reinterpret_cast<void **>(&d_A), mem_size_A));

	checkCudaErrors(cudaMalloc(reinterpret_cast<void **>(&d_B), mem_size_B));

	checkCudaErrors(cudaMalloc(reinterpret_cast<void **>(&d_C), mem_size_C));

	// copy host memory to device
	checkCudaErrors(cudaMemcpy(d_A, h_A, mem_size_A, cudaMemcpyHostToDevice));

	checkCudaErrors(cudaMemcpy(d_B, h_B, mem_size_B, cudaMemcpyHostToDevice));

	// Setup execution parameters
	dim3 threads(block_size, block_size);
	dim3 grid(dimsB.x / threads.x, dimsA.y / threads.y);

	// Create and start timer
	printf("Computing result using CUDA Kernel...\n");

	// Performs warmup operation using matrixMul CUDA kernel
	if (block_size == 16) {
		MatrixMulCUDA<16> << < grid, threads >> > (d_C, d_A, d_B,
			dimsA.x, dimsB.x);
	}
	else {
		MatrixMulCUDA<32> << < grid, threads >> > (d_C, d_A, d_B,
			dimsA.x, dimsB.x);
	}

	printf("done\n");

	cudaDeviceSynchronize();

	// Allocate CUDA events that we'll use for timing
	cudaEvent_t start;
	checkCudaErrors(cudaEventCreate(&start));

	cudaEvent_t stop;
	checkCudaErrors(cudaEventCreate(&stop));

	// Record the start event
	checkCudaErrors(cudaEventRecord(start, NULL));

	// Execute the kernel
	int nIter = 300;

	for (int j = 0; j < nIter; j++) {
		if (block_size == 16) {
			MatrixMulCUDA<16> << < grid, threads >> > (d_C, d_A, d_B,
				dimsA.x, dimsB.x);
		}
		else {
			MatrixMulCUDA<32> << < grid, threads >> > (d_C, d_A, d_B,
				dimsA.x, dimsB.x);
		}
	}

	// Record the stop event
	checkCudaErrors(cudaEventRecord(stop, NULL));

	// Wait for the stop event to complete
	checkCudaErrors(cudaEventSynchronize(stop));

	float msecTotal = 0.0f;
	checkCudaErrors(cudaEventElapsedTime(&msecTotal, start, stop));

	// Compute and print the performance
	float msecPerMatrixMul = msecTotal / nIter;
	double flopsPerMatrixMul = 2.0 * static_cast<double>(dimsA.x) *
		static_cast<double>(dimsA.y) *
		static_cast<double>(dimsB.x);
	double gigaFlops = (flopsPerMatrixMul * 1.0e-9f) /
		(msecPerMatrixMul / 1000.0f);
	printf(
		"Performance= %.2f GFlop/s, Time= %.3f msec, Size= %.0f Ops," \
		" WorkgroupSize= %u threads/block\n",
		gigaFlops,
		msecPerMatrixMul,
		flopsPerMatrixMul,
		threads.x * threads.y);

	// Copy result from device to host
	checkCudaErrors(cudaMemcpy(h_C, d_C, mem_size_C, cudaMemcpyDeviceToHost));

	printf("Checking computed result for correctness: ");
	bool correct = true;

	// test relative error by the formula
	//     |<x, y>_cpu - <x,y>_gpu|/<|x|, |y|>  < eps
	double eps = 1.e-6;  // machine zero

	for (int i = 0; i < static_cast<int>(dimsC.x * dimsC.y); i++) {
		double abs_err = fabs(h_C[i] - (dimsA.x * valB));
		double dot_length = dimsA.x;
		double abs_val = fabs(h_C[i]);
		double rel_err = abs_err / abs_val / dot_length;

		if (rel_err > eps) {
			printf("Error! Matrix[%05d]=%.8f, ref=%.8f error term is > %E\n",
				i, h_C[i], dimsA.x * valB, eps);
			correct = false;
		}
	}

	printf("%s\n", correct ? "Result = PASS" : "Result = FAIL");

	// Clean up memory
	free(h_A);
	free(h_B);
	free(h_C);
	checkCudaErrors(cudaFree(d_A));
	checkCudaErrors(cudaFree(d_B));
	checkCudaErrors(cudaFree(d_C));

	printf("\nNOTE: The CUDA Samples are not meant for performance"\
		"measurements. Results may vary when GPU Boost is enabled.\n");

	if (correct) {
		return EXIT_SUCCESS;
	}
	else {
		return EXIT_FAILURE;
	}
}


/**
 * Program main
 */
int main(int argc, char **argv) {
	printf("[Matrix Multiply Using CUDA] - Starting...\n");

	if (checkCmdLineFlag(argc, (const char **)argv, "help") ||
		checkCmdLineFlag(argc, (const char **)argv, "?")) {
		printf("Usage -device=n (n >= 0 for deviceID)\n");
		printf("      -wA=WidthA -hA=HeightA (Width x Height of Matrix A)\n");
		printf("      -wB=WidthB -hB=HeightB (Width x Height of Matrix B)\n");
		printf("  Note: Outer matrix dimensions of A & B matrices" \
			" must be equal.\n");

		exit(EXIT_SUCCESS);
	}

	// This will pick the best possible CUDA capable device, otherwise
	// override the device ID based on input provided at the command line
	int dev = findCudaDevice(argc, (const char **)argv);

	int block_size = 32;

	dim3 dimsA(5 * 2 * block_size, 5 * 2 * block_size, 1);
	dim3 dimsB(5 * 2 * block_size, 5 * 2 * block_size, 1);

	// width of Matrix A
	if (checkCmdLineFlag(argc, (const char **)argv, "wA")) {
		dimsA.x = getCmdLineArgumentInt(argc, (const char **)argv, "wA");
	}

	// height of Matrix A
	if (checkCmdLineFlag(argc, (const char **)argv, "hA")) {
		dimsA.y = getCmdLineArgumentInt(argc, (const char **)argv, "hA");
	}

	// width of Matrix B
	if (checkCmdLineFlag(argc, (const char **)argv, "wB")) {
		dimsB.x = getCmdLineArgumentInt(argc, (const char **)argv, "wB");
	}

	// height of Matrix B
	if (checkCmdLineFlag(argc, (const char **)argv, "hB")) {
		dimsB.y = getCmdLineArgumentInt(argc, (const char **)argv, "hB");
	}

	if (dimsA.x != dimsB.y) {
		printf("Error: outer matrix dimensions must be equal. (%d != %d)\n",
			dimsA.x, dimsB.y);
		exit(EXIT_FAILURE);
	}

	printf("MatrixA(%d,%d), MatrixB(%d,%d)\n", dimsA.x, dimsA.y,
		dimsB.x, dimsB.y);

	int matrix_result = MatrixMultiply(argc, argv, block_size, dimsA, dimsB);

	exit(matrix_result);
}

