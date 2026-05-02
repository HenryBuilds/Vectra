# Minimal Terraform / HCL fixture for language parse-validation tests.

terraform {
  required_version = ">= 1.5.0"
}

variable "region" {
  type    = string
  default = "eu-central-1"
}

provider "demo" {
  region = var.region
}

resource "demo_bucket" "logs" {
  name = "demo-logs"
  tags = {
    Environment = "test"
  }
}

module "network" {
  source = "./modules/network"
  cidr   = "10.0.0.0/16"
}
