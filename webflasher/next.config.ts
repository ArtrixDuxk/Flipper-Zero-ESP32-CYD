import type {NextConfig} from "next";

const basePath = process.env.NEXT_PUBLIC_BASE_PATH?.replace(/\/$/, "") ?? "";

const nextConfig: NextConfig = {
  output: "export",
  poweredByHeader: false,
  trailingSlash: true,
  basePath,
  assetPrefix: basePath || undefined,
};

export default nextConfig;
